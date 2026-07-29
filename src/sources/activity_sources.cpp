#include "activity_sources.hpp"

#include "../hook/uiohook_helper.hpp"
#include "../input/input_data.hpp"
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
#include <QStringList>
#include <QSvgGenerator>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_set>
#include <vector>
#include <obs-module.h>
#include <uiohook.h>
#include <util/platform.h>

extern "C" {
#include <graphics/graphics.h>
}

namespace sources {
namespace {
constexpr uint64_t minute_ns = 60ULL * 1000 * 1000 * 1000;
constexpr uint64_t max_heatmap_gap_ns = 250ULL * 1000 * 1000;
constexpr qreal default_heatmap_hex_radius = 10.0;

QColor obs_color(uint32_t color)
{
	return {static_cast<int>(color & 0xff), static_cast<int>((color >> 8) & 0xff),
		static_cast<int>((color >> 16) & 0xff), static_cast<int>((color >> 24) & 0xff)};
}

void migrate_legacy_colors(obs_data_t *settings, const char *migration_key,
			   std::initializer_list<const char *> color_keys)
{
	if (obs_data_get_bool(settings, migration_key))
		return;
	for (const char *color_key : color_keys) {
		const uint32_t color = static_cast<uint32_t>(obs_data_get_int(settings, color_key));
		obs_data_set_int(settings, color_key, color | 0xff000000);
	}
	obs_data_set_bool(settings, migration_key, true);
}

class activity_source {
public:
	activity_source(obs_source_t *source, obs_data_t *settings, bool register_hotkeys = true,
			bool initial_update = true)
		: source(source)
	{
		{
			std::lock_guard<std::mutex> lock(activity_sources_mutex);
			activity_sources.insert(this);
		}
		if (register_hotkeys)
			reset_hotkey = obs_hotkey_register_source(
				source, "reset_input_activity", obs_module_text("Activity.ResetHotkey"),
				[](void *, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
					if (pressed)
						reset_all_activity();
				},
				this);
		if (register_hotkeys)
			lap_hotkey = obs_hotkey_register_source(
				source, "lap_input_activity", obs_module_text("Activity.LapHotkey"),
				[](void *, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
					if (pressed)
						lap_all_activity();
				},
				this);
		if (initial_update)
			obs_source_update(source, settings);
	}
	virtual ~activity_source()
	{
		{
			std::lock_guard<std::mutex> lock(activity_sources_mutex);
			activity_sources.erase(this);
		}
		obs_hotkey_unregister(reset_hotkey);
		obs_hotkey_unregister(lap_hotkey);
		if (texture) {
			obs_enter_graphics();
			gs_texture_destroy(texture);
			obs_leave_graphics();
		}
	}
	virtual void update(obs_data_t *settings)
	{
		migrate_legacy_colors(settings, "activity.colors_with_alpha", {"activity.text_color"});
		width = std::max(1, static_cast<int>(obs_data_get_int(settings, "activity.width")));
		height = std::max(1, static_cast<int>(obs_data_get_int(settings, "activity.height")));
		padding = std::max(0, static_cast<int>(obs_data_get_int(settings, "activity.padding")));
		font_size = std::max(8, static_cast<int>(obs_data_get_int(settings, "activity.font_size")));
		text_color = obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "activity.text_color")));
		background_color =
			obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "activity.background_color")));
		text_shadow = obs_data_get_bool(settings, "activity.text_shadow");
		text_shadow_color =
			obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "activity.text_shadow_color")));
		text_shadow_offset =
			std::max(0, static_cast<int>(obs_data_get_int(settings, "activity.text_shadow_offset")));
		title = QString::fromUtf8(obs_data_get_string(settings, "activity.title"));
		show_title = obs_data_get_bool(settings, "activity.show_title");
		font_family = "Silom";
		if (auto *font = obs_data_get_obj(settings, "activity.font")) {
			const QString saved_face = QString::fromUtf8(obs_data_get_string(font, "face"));
			if (!saved_face.isEmpty())
				font_family = saved_face;
			obs_data_release(font);
		}
		const std::string selected_target_type = obs_data_get_string(settings, "activity.target.type");
		target = selected_target_type == "display"       ? target_type::display
			 : selected_target_type == "application" ? target_type::application
			 : selected_target_type == "window"      ? target_type::window
								 : target_type::all;
		target_display = static_cast<uint32_t>(obs_data_get_int(settings, "activity.target.display"));
		target_application = obs_data_get_string(settings, "activity.target.application");
		const std::string window = obs_data_get_string(settings, "activity.target.window");
		const size_t delimiter = window.rfind('#');
		if (delimiter == std::string::npos) {
			target_window_application.clear();
			target_window = 0;
		} else {
			target_window_application = window.substr(0, delimiter);
			try {
				target_window = std::stoull(window.substr(delimiter + 1));
			} catch (...) {
				target_window = 0;
			}
		}
	}
	virtual void tick(float) { consume_events(); }
	void draw(gs_effect_t *effect)
	{
		image = QImage(width, height, QImage::Format_RGBA8888);
		image.fill(background_color);
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing);
		if (show_title && !title.isEmpty()) {
			painter.setFont(font());
			const int title_height = title_content_offset() - padding / 2;
			draw_text(painter, QRect(padding, padding, std::max(1, width - padding * 2), title_height),
				  Qt::AlignLeft | Qt::AlignVCenter, title, text_color);
			painter.translate(0, title_content_offset());
			painter.setClipRect(0, 0, width, std::max(1, height - title_content_offset()));
		}
		render(painter);
		if (texture && (texture_width != width || texture_height != height)) {
			gs_texture_destroy(texture);
			texture = nullptr;
		}
		if (!texture) {
			texture = gs_texture_create(width, height, GS_RGBA, 1, nullptr, GS_DYNAMIC);
			texture_width = width;
			texture_height = height;
		}
		if (!texture)
			return;
		gs_texture_set_image(texture, image.constBits(), static_cast<uint32_t>(image.bytesPerLine()), false);
		gs_blend_state_push();
		gs_enable_blending(true);
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), texture);
		gs_draw_sprite(texture, 0, width, height);
		gs_blend_state_pop();
	}
	void consume_events()
	{
		std::vector<input_data::trace_event> events;
		input_data::button_map<uint16_t> keyboard;
		input_data::button_map<uint16_t> mouse;
		std::lock_guard<std::mutex> lock(local_data::data.m_mutex);
		local_data::data.events_after(cursor, events);
		keyboard = local_data::data.keyboard;
		mouse = local_data::data.mouse;
		for (const auto &event : events)
			if (matches(event))
				on_event(event);
		if (events.empty() || matches(events.back()))
			on_snapshot(keyboard, mouse);
	}
	virtual void on_event(const input_data::trace_event &) {}
	virtual void on_snapshot(const input_data::button_map<uint16_t> &, const input_data::button_map<uint16_t> &) {}
	virtual void reset_activity() {}
	virtual void lap_activity() {}
	virtual void render(QPainter &) = 0;
	static void reset_all_activity()
	{
		std::lock_guard<std::mutex> lock(activity_sources_mutex);
		for (auto *activity : activity_sources)
			activity->reset_activity();
	}
	static void lap_all_activity()
	{
		std::lock_guard<std::mutex> lock(activity_sources_mutex);
		for (auto *activity : activity_sources)
			activity->lap_activity();
	}
	QFont font(int pixel_size = -1) const
	{
		QFont result(font_family);
		result.setBold(true);
		result.setPixelSize(pixel_size > 0 ? pixel_size : font_size);
		return result;
	}
	int title_content_offset() const
	{
		return show_title && !title.isEmpty() ? QFontMetrics(font()).lineSpacing() + padding : 0;
	}
	void draw_text(QPainter &painter, const QRect &rect, int alignment, const QString &text,
		       const QColor &color) const
	{
		if (text_shadow) {
			QColor shadow = text_shadow_color;
			shadow.setAlpha(shadow.alpha() * color.alpha() / 255);
			painter.setPen(shadow);
			painter.drawText(rect.translated(text_shadow_offset, text_shadow_offset), alignment, text);
		}
		painter.setPen(color);
		painter.drawText(rect, alignment, text);
	}
	obs_source_t *source{};
	int width = 480, height = 180, padding = 12, font_size = 28;
	QColor text_color{255, 255, 255};
	QColor background_color{0, 0, 0, 0};
	QColor text_shadow_color{0, 0, 0, 204};
	bool text_shadow{};
	int text_shadow_offset{2};
	QString font_family;
	QString title;
	bool show_title{};
	uint64_t cursor{};

private:
	enum class target_type { all, display, application, window };
	bool matches(const input_data::trace_event &event) const
	{
		switch (target) {
		case target_type::all:
			return true;
		case target_type::display: {
			const bool mouse_motion = event.type == EVENT_MOUSE_MOVED || event.type == EVENT_MOUSE_DRAGGED;
			return (mouse_motion ? event.pointer_display_id : event.focused_display_id) == target_display;
		}
		case target_type::application:
			return !target_application.empty() && event.application_id == target_application;
		case target_type::window:
			return target_window != 0 && event.application_id == target_window_application &&
			       event.window_id == target_window;
		}
		return false;
	}
	static std::mutex activity_sources_mutex;
	static std::unordered_set<activity_source *> activity_sources;
	obs_hotkey_id reset_hotkey = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id lap_hotkey = OBS_INVALID_HOTKEY_ID;
	target_type target{target_type::all};
	uint32_t target_display{};
	uint64_t target_window{};
	std::string target_application;
	std::string target_window_application;
	QImage image;
	gs_texture_t *texture{};
	int texture_width{}, texture_height{};
};

std::mutex activity_source::activity_sources_mutex;
std::unordered_set<activity_source *> activity_source::activity_sources;

QString key_name(const input_data::trace_event &event)
{
	if (event.keychar >= 32 && event.keychar < 127)
		return QString(QChar(event.keychar)).toUpper();
	if (event.code >= VC_A && event.code <= VC_Z)
		return QString(QChar('A' + event.code - VC_A));
	if (event.code >= VC_0 && event.code <= VC_9)
		return QString(QChar('0' + event.code - VC_0));
	if (event.code >= VC_F1 && event.code <= VC_F12)
		return QString("F%1").arg(event.code - VC_F1 + 1);
	if (event.code >= VC_F13 && event.code <= VC_F24)
		return QString("F%1").arg(event.code - VC_F13 + 13);
	if (event.code >= VC_KP_0 && event.code <= VC_KP_9)
		return QString("Num %1").arg(event.code - VC_KP_0);
	switch (event.code) {
	case VC_SHIFT_L:
	case VC_SHIFT_R:
		return "Shift";
	case VC_CONTROL_L:
	case VC_CONTROL_R:
		return "Ctrl";
	case VC_ALT_L:
	case VC_ALT_R:
		return "Opt";
	case VC_META_L:
	case VC_META_R:
		return "Cmd";
	case VC_SPACE:
		return "Space";
	case VC_ENTER:
		return "Return";
	case VC_ESCAPE:
		return "Esc";
	case VC_BACKSPACE:
		return "Del";
	case VC_TAB:
		return "Tab";
	case VC_CAPS_LOCK:
		return "Caps";
	case VC_MINUS:
		return "-";
	case VC_EQUALS:
		return "=";
	case VC_OPEN_BRACKET:
		return "[";
	case VC_CLOSE_BRACKET:
		return "]";
	case VC_BACK_SLASH:
		return "\\";
	case VC_SEMICOLON:
		return ";";
	case VC_QUOTE:
		return "'";
	case VC_COMMA:
		return ",";
	case VC_PERIOD:
		return ".";
	case VC_SLASH:
		return "/";
	case VC_BACK_QUOTE:
		return "`";
	case VC_UP:
		return "Up";
	case VC_DOWN:
		return "Down";
	case VC_LEFT:
		return "Left";
	case VC_RIGHT:
		return "Right";
	case VC_HOME:
		return "Home";
	case VC_END:
		return "End";
	case VC_PAGE_UP:
		return "PgUp";
	case VC_PAGE_DOWN:
		return "PgDn";
	case VC_INSERT:
		return "Insert";
	case VC_DELETE:
		return "Delete";
	case VC_PRINT_SCREEN:
		return "PrtSc";
	case VC_SCROLL_LOCK:
		return "Scroll";
	case VC_PAUSE:
		return "Pause";
	case VC_NUM_LOCK:
		return "NumLock";
	case VC_KP_DIVIDE:
		return "Num /";
	case VC_KP_MULTIPLY:
		return "Num *";
	case VC_KP_SUBTRACT:
		return "Num -";
	case VC_KP_ADD:
		return "Num +";
	case VC_KP_DECIMAL:
		return "Num .";
	case VC_KP_ENTER:
		return "Num Enter";
	default:
		return QString("Unknown key");
	}
}

bool is_alphanumeric_key(const input_data::trace_event &event)
{
	return ((event.keychar >= 'A' && event.keychar <= 'Z') || (event.keychar >= 'a' && event.keychar <= 'z') ||
		(event.keychar >= '0' && event.keychar <= '9')) ||
	       (event.code >= VC_A && event.code <= VC_Z) || (event.code >= VC_0 && event.code <= VC_9) ||
	       (event.code >= VC_KP_0 && event.code <= VC_KP_9);
}

class live_keys_source final : public activity_source {
public:
	enum class fade_curve { linear, ease_in, ease_out, ease_in_out };

	live_keys_source(obs_source_t *source, obs_data_t *settings, bool register_hotkeys = true,
			 bool initial_update = true)
		: activity_source(source, settings, register_hotkeys, initial_update)
	{
	}
	void update(obs_data_t *settings) override
	{
		activity_source::update(settings);
		migrate_legacy_colors(settings, "live_keys.colors_with_alpha", {"live_keys.color"});
		maximum = std::max(1, static_cast<int>(obs_data_get_int(settings, "live_keys.maximum")));
		most_used_maximum = std::max(1, static_cast<int>(obs_data_get_int(settings, "live_keys.top_n")));
		live_row_height = std::max(24, static_cast<int>(obs_data_get_int(settings, "live_keys.row_height")));
		chart_alignment = std::string(obs_data_get_string(settings, "live_keys.top_n_alignment")) == "right"
					  ? Qt::AlignRight
					  : Qt::AlignLeft;
		element_spacing =
			std::clamp(static_cast<int>(obs_data_get_int(settings, "live_keys.element_spacing")), 0, 200);
		show_most_used = obs_data_get_bool(settings, "live_keys.show_most_used");
		live_title = QString::fromUtf8(obs_data_get_string(settings, "live_keys.live_title"));
		show_live_title = obs_data_get_bool(settings, "live_keys.show_live_title");
		live_title_font_size =
			std::max(8, static_cast<int>(obs_data_get_int(settings, "live_keys.live_title_font_size")));
		most_used_title = QString::fromUtf8(obs_data_get_string(settings, "live_keys.most_used_title"));
		show_most_used_title = obs_data_get_bool(settings, "live_keys.show_most_used_title");
		most_used_title_font_size = std::max(
			8, static_cast<int>(obs_data_get_int(settings, "live_keys.most_used_title_font_size")));
		key_font_size = std::max(8, static_cast<int>(obs_data_get_int(settings, "live_keys.key_font_size")));
		special_key_font_size =
			std::max(8, static_cast<int>(obs_data_get_int(settings, "live_keys.special_key_font_size")));
		total_font_size =
			std::max(8, static_cast<int>(obs_data_get_int(settings, "live_keys.total_font_size")));
		fade_duration_ns =
			static_cast<uint64_t>(std::max<int64_t>(0, obs_data_get_int(settings, "live_keys.fade_ms"))) *
			1000 * 1000;
		const std::string curve = obs_data_get_string(settings, "live_keys.fade_curve");
		if (curve == "ease_in")
			fade = fade_curve::ease_in;
		else if (curve == "ease_out")
			fade = fade_curve::ease_out;
		else if (curve == "ease_in_out")
			fade = fade_curve::ease_in_out;
		else
			fade = fade_curve::linear;
		theme_color = obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "live_keys.color")));
		pressed_color = obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "live_keys.pressed_color")));
	}
	void on_event(const input_data::trace_event &event) override
	{
		if (event.type == EVENT_KEY_PRESSED && !held[event.code]) {
			held[event.code] = true;
			key_labels[event.code] = key_name(event);
			ordered.erase(std::remove_if(ordered.begin(), ordered.end(),
						     [&event](const auto &key) { return key.code == event.code; }),
				      ordered.end());
			ordered.push_back({event.code, key_labels[event.code], is_alphanumeric_key(event), 0,
					   ++press_counts[event.code]});
		} else if (event.type == EVENT_KEY_RELEASED) {
			held[event.code] = false;
			for (auto &key : ordered) {
				if (key.code == event.code)
					key.fade_until = event.time_ns + fade_duration_ns;
			}
		}
	}
	void on_snapshot(const input_data::button_map<uint16_t> &keyboard,
			 const input_data::button_map<uint16_t> &) override
	{
		const uint64_t now = os_gettime_ns();
		for (auto it = ordered.begin(); it != ordered.end();) {
			const auto pressed = keyboard.find(it->code);
			if (pressed == keyboard.end() || !pressed->second) {
				held[it->code] = false;
				if (it->fade_until == 0)
					it->fade_until = now + fade_duration_ns;
				if (it->fade_until <= now)
					it = ordered.erase(it);
				else
					++it;
			} else {
				++it;
			}
		}
		for (const auto &[code, pressed] : keyboard) {
			if (pressed && !held[code]) {
				held[code] = true;
				input_data::trace_event event{};
				event.code = code;
				key_labels.try_emplace(code, key_name(event));
				ordered.push_back(
					{code, key_labels[code], is_alphanumeric_key(event), 0, press_counts[code]});
			}
		}
	}
	void reset_activity() override
	{
		press_counts.clear();
		key_labels.clear();
		for (auto &key : ordered)
			key.press_count = 0;
	}
	void render(QPainter &painter) override
	{
		std::vector<active_key> keys = ordered;
		const int live_title_height =
			show_live_title && !live_title.isEmpty() ? live_title_font_size + padding / 2 : 0;
		const int chart_title_height = show_most_used && show_most_used_title && !most_used_title.isEmpty()
						       ? most_used_title_font_size + padding / 2
						       : 0;
		const int live_height =
			show_most_used
				? std::min(live_row_height, std::max(1, height - padding * 2 - live_title_height -
										chart_title_height - element_spacing))
				: std::max(1, height - padding * 2 - live_title_height);
		if (live_title_height) {
			painter.setFont(font(live_title_font_size));
			draw_text(painter, QRect(padding, padding, width - padding * 2, live_title_height),
				  Qt::AlignLeft | Qt::AlignVCenter, live_title, text_color);
		}
		const int start = std::max(0, static_cast<int>(keys.size()) - maximum);
		const int available_span = width - padding * 2;
		const int gap =
			std::min(element_spacing, std::max(0, (available_span - maximum) / std::max(1, maximum - 1)));
		const int key_width = std::max(1, (width - padding * 2 - gap * (maximum - 1)) / maximum);
		const int cell_height = live_height;
		painter.setFont(font(total_font_size));
		const int total_height = std::min(QFontMetrics(painter.font()).height(), std::max(0, cell_height - 9));
		const int key_height = std::max(1, cell_height - total_height - gap);
		const uint64_t now = os_gettime_ns();
		for (int index = start; index < static_cast<int>(keys.size()); ++index) {
			const int position = index - start;
			const int x = padding + position * (key_width + gap);
			const int y = padding + live_title_height;
			const QRect total(x, y, key_width, total_height);
			const QRect row(x, y + total_height + gap, key_width, key_height);
			const auto &key = keys[index];
			const int alpha =
				key.fade_until > now && fade_duration_ns > 0
					? static_cast<int>(255 * fade_alpha(static_cast<double>(key.fade_until - now) /
									    fade_duration_ns))
					: (key.fade_until ? 0 : 255);
			QColor fill = held[key.code] ? pressed_color : theme_color;
			fill.setAlpha(std::clamp(alpha, 0, 255));
			QColor text = text_color;
			text.setAlpha(std::clamp(alpha, 0, 255));
			painter.setBrush(fill);
			painter.setPen(Qt::NoPen);
			painter.drawRoundedRect(row, 6, 6);
			painter.setFont(font(total_font_size));
			draw_text(painter, total, Qt::AlignCenter, QString::number(key.press_count), text);
			painter.setFont(font(key.alphanumeric ? key_font_size : special_key_font_size));
			draw_text(painter, row, Qt::AlignCenter, key.label, text);
		}
		if (show_most_used) {
			const int chart_top = padding + live_title_height + live_height + element_spacing;
			if (chart_title_height) {
				painter.setFont(font(most_used_title_font_size));
				draw_text(painter, QRect(padding, chart_top, width - padding * 2, chart_title_height),
					  Qt::AlignLeft | Qt::AlignVCenter, most_used_title, text_color);
			}
			draw_most_used_chart(painter,
					     QRect(padding, chart_top + chart_title_height,
						   std::max(1, width - padding * 2),
						   std::max(1, height - padding * 2 - live_title_height - live_height -
								       chart_title_height - element_spacing)));
		}
	}

private:
	void draw_most_used_chart(QPainter &painter, const QRect &bounds) const
	{
		std::vector<active_key> keys;
		for (const auto &[code, count] : press_counts) {
			if (count == 0)
				continue;
			input_data::trace_event event{};
			event.code = code;
			keys.push_back({code, key_labels.count(code) ? key_labels.at(code) : key_name(event),
					is_alphanumeric_key(event), 0, count});
		}
		std::sort(keys.begin(), keys.end(), [](const active_key &left, const active_key &right) {
			return left.press_count != right.press_count ? left.press_count > right.press_count
								     : left.code < right.code;
		});
		keys.resize(std::min(keys.size(), static_cast<size_t>(most_used_maximum)));
		if (keys.empty())
			return;
		const uint64_t highest = keys.front().press_count;
		const int gap =
			std::min(element_spacing, std::max(0, bounds.height() / static_cast<int>(keys.size() * 3)));
		const int row_height = std::max(1, (bounds.height() - gap * (static_cast<int>(keys.size()) - 1)) /
							   static_cast<int>(keys.size()));
		const QFontMetrics metrics(font(key_font_size));
		const int value_height = std::min(metrics.height(), row_height / 2);
		const int chart_width = bounds.width();
		const bool right_aligned = chart_alignment == Qt::AlignRight;
		const int chart_left = bounds.left();
		for (size_t index = 0; index < keys.size(); ++index) {
			const int top = bounds.top() + static_cast<int>(index) * (row_height + gap);
			const int bar_width =
				std::max(1, static_cast<int>(chart_width * keys[index].press_count / highest));
			const QRect bar(right_aligned ? chart_left + chart_width - bar_width : chart_left,
					top + value_height, bar_width, std::max(1, row_height - value_height));
			painter.setPen(Qt::NoPen);
			const auto pressed = held.find(keys[index].code);
			painter.setBrush(pressed != held.end() && pressed->second ? pressed_color : theme_color);
			painter.drawRoundedRect(bar, 3, 3);
			painter.setFont(font(keys[index].alphanumeric ? key_font_size : special_key_font_size));
			draw_text(painter, QRect(chart_left, top, chart_width, value_height),
				  Qt::AlignLeft | Qt::AlignVCenter, keys[index].label, text_color);
			painter.setFont(font(total_font_size));
			draw_text(painter, QRect(chart_left, top, chart_width, value_height),
				  Qt::AlignRight | Qt::AlignVCenter, QString::number(keys[index].press_count),
				  text_color);
		}
	}
	double fade_alpha(double remaining) const
	{
		remaining = std::clamp(remaining, 0.0, 1.0);
		switch (fade) {
		case fade_curve::ease_in:
			return 1.0 - (1.0 - remaining) * (1.0 - remaining);
		case fade_curve::ease_out:
			return remaining * remaining;
		case fade_curve::ease_in_out: {
			const double elapsed = 1.0 - remaining;
			const double eased_elapsed = elapsed < 0.5 ? 2.0 * elapsed * elapsed
								   : 1.0 - std::pow(-2.0 * elapsed + 2.0, 2.0) / 2.0;
			return 1.0 - eased_elapsed;
		}
		case fade_curve::linear:
			return remaining;
		}
		return remaining;
	}
	struct active_key {
		uint16_t code;
		QString label;
		bool alphanumeric;
		uint64_t fade_until;
		uint64_t press_count;
	};
	int maximum = 8;
	int most_used_maximum = 8;
	int live_row_height = 96;
	int live_title_font_size = 28, most_used_title_font_size = 28;
	int element_spacing = 2;
	int key_font_size = 36;
	int special_key_font_size = 28;
	int total_font_size = 24;
	uint64_t fade_duration_ns = 300ULL * 1000 * 1000;
	fade_curve fade{fade_curve::linear};
	QColor theme_color{37, 99, 235};
	QColor pressed_color{239, 68, 68};
	std::unordered_map<uint16_t, bool> held;
	std::unordered_map<uint16_t, uint64_t> press_counts;
	std::unordered_map<uint16_t, QString> key_labels;
	std::vector<active_key> ordered;
	bool show_most_used{};
	bool show_live_title{true}, show_most_used_title{true};
	Qt::Alignment chart_alignment{Qt::AlignLeft};
	QString live_title{"Live Keys"}, most_used_title{"Most Used Keys"};
};

class mouse_activity_source final : public activity_source {
public:
	mouse_activity_source(obs_source_t *source, obs_data_t *settings, bool register_hotkeys = true,
			      bool initial_update = true)
		: activity_source(source, settings, register_hotkeys, initial_update)
	{
		if (register_hotkeys)
			export_hotkey = obs_hotkey_register_source(
				source, "export_mouse_heatmap", obs_module_text("MouseActivity.ExportHotkey"),
				[](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
					if (pressed)
						static_cast<mouse_activity_source *>(data)->export_heatmap();
				},
				this);
	}
	~mouse_activity_source() override { obs_hotkey_unregister(export_hotkey); }
	void update(obs_data_t *settings) override
	{
		const QRect previous_heatmap = heatmap_rect();
		const qreal previous_hex_radius = hex_radius;
		activity_source::update(settings);
		migrate_legacy_colors(settings, "mouse_activity.colors_with_alpha",
				      {"mouse_activity.color", "mouse_activity.left_color",
				       "mouse_activity.right_color", "mouse_activity.middle_color"});
		left_label = QString::fromUtf8(obs_data_get_string(settings, "mouse_activity.left_label"));
		right_label = QString::fromUtf8(obs_data_get_string(settings, "mouse_activity.right_label"));
		middle_label = QString::fromUtf8(obs_data_get_string(settings, "mouse_activity.middle_label"));
		show_coordinates = obs_data_get_bool(settings, "mouse_activity.show_coordinates");
		show_heatmap = obs_data_get_bool(settings, "mouse_activity.show_heatmap");
		show_live_mouse = obs_data_get_bool(settings, "mouse_activity.show_live_mouse");
		show_distance = obs_data_get_bool(settings, "mouse_activity.show_distance");
		const std::string info_alignment = obs_data_get_string(settings, "mouse_activity.info_alignment");
		information_alignment = info_alignment == "right" ? Qt::AlignRight : Qt::AlignLeft;
		distance_unit = obs_data_get_string(settings, "mouse_activity.distance_unit");
		if (distance_unit != "metric" && distance_unit != "imperial")
			distance_unit = "pixels";
		mouse_dpi = std::max<int64_t>(1, obs_data_get_int(settings, "mouse_activity.mouse_dpi"));
		heatmap_gradient = obs_data_get_string(settings, "mouse_activity.heatmap_gradient");
		hex_radius = std::clamp(static_cast<qreal>(obs_data_get_int(settings, "mouse_activity.hex_size")), 1.0,
					100.0);
		heatmap_opacity =
			std::clamp(static_cast<int>(obs_data_get_int(settings, "mouse_activity.opacity")), 0, 100);
		export_directory = QString::fromUtf8(obs_data_get_string(settings, "mouse_activity.export_directory"));
		export_svg = std::string(obs_data_get_string(settings, "mouse_activity.export_format")) == "svg";
		trail_duration_ns = static_cast<uint64_t>(
			std::max<int64_t>(100, obs_data_get_int(settings, "mouse_activity.trail_ms")) * 1000 * 1000);
		active_color = obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "mouse_activity.color")));
		button_colors[MOUSE_BUTTON1] =
			obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "mouse_activity.left_color")));
		button_colors[MOUSE_BUTTON2] =
			obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "mouse_activity.right_color")));
		button_colors[MOUSE_BUTTON3] =
			obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "mouse_activity.middle_color")));
		const bool new_map_clicks = std::string(obs_data_get_string(settings, "mouse_activity.map")) ==
					    "clicks";
		const int new_display = static_cast<int>(obs_data_get_int(settings, "mouse_activity.display"));
		const bool display_changed = new_display != display;
		if (display_changed || new_map_clicks != map_clicks) {
			coordinates.reset();
			clear();
		}
		map_clicks = new_map_clicks;
		display = new_display;
		load_display();
		update_dimensions();
		show_border = obs_data_get_bool(settings, "mouse_activity.show_border");
		show_center_mark = obs_data_get_bool(settings, "mouse_activity.show_center_mark");
		if (hex_radius != previous_hex_radius)
			heatmap_bounds = {};
		resize_heatmap();
		if (display_changed || heatmap_rect() != previous_heatmap || hex_radius != previous_hex_radius)
			trail.clear();
	}
	void clear()
	{
		for (auto &bin : hex_bins)
			bin.value = 0;
		last_motion.reset();
	}
	void export_current_heatmap() const { export_heatmap(); }
	void reset_activity() override
	{
		clear();
		trail.clear();
		distance = 0;
		last_distance.reset();
	}
	void on_event(const input_data::trace_event &event) override
	{
		if (event.type == EVENT_MOUSE_PRESSED || event.type == EVENT_MOUSE_RELEASED)
			buttons[event.code] = event.type == EVENT_MOUSE_PRESSED;
		const bool moved = event.type == EVENT_MOUSE_MOVED || event.type == EVENT_MOUSE_DRAGGED;
		const bool clicked = event.type == EVENT_MOUSE_PRESSED && event.code >= MOUSE_BUTTON1 &&
				     event.code <= MOUSE_BUTTON3;
		if ((!moved && !clicked) || monitor.width == 0)
			return;
		if (event.x < monitor.x || event.y < monitor.y || event.x >= monitor.x + monitor.width ||
		    event.y >= monitor.y + monitor.height) {
			coordinates.reset();
			last_motion.reset();
			last_distance.reset();
			return;
		}
		const int relative_x = event.x - monitor.x;
		const int relative_y = event.y - monitor.y;
		coordinates = QPoint(relative_x, relative_y);
		const QRect heatmap = heatmap_rect();
		const QPoint point(heatmap.x() + relative_x * heatmap.width() / monitor.width,
				   heatmap.y() + relative_y * heatmap.height() / monitor.height);
		if (moved) {
			if (last_distance)
				distance +=
					std::hypot(relative_x - last_distance->x(), relative_y - last_distance->y());
			last_distance = QPoint(relative_x, relative_y);
			const qreal maximum_trail_jump = std::hypot(heatmap.width(), heatmap.height()) / 3.0;
			if (!trail.empty() && std::hypot(point.x() - trail.back().second.x(),
							 point.y() - trail.back().second.y()) > maximum_trail_jump)
				trail.clear();
			trail.push_back({event.time_ns, point});
		}
		const size_t hex_index = nearest_hex(point);
		if (map_clicks && clicked) {
			++hex_bins[hex_index].value;
		} else if (!map_clicks && moved && last_motion && event.time_ns > last_motion->time_ns) {
			const uint64_t duration = std::min(event.time_ns - last_motion->time_ns, max_heatmap_gap_ns);
			hex_bins[last_motion->hex_index].value += duration;
		}
		if (moved)
			last_motion = motion_point{event.time_ns, hex_index};
	}
	void on_snapshot(const input_data::button_map<uint16_t> &,
			 const input_data::button_map<uint16_t> &mouse) override
	{
		for (uint16_t button = MOUSE_BUTTON1; button <= MOUSE_BUTTON3; ++button) {
			const auto pressed = mouse.find(button);
			buttons[button] = pressed != mouse.end() && pressed->second;
		}
	}
	void tick(float seconds) override
	{
		activity_source::tick(seconds);
		const uint64_t now = os_gettime_ns();
		while (!trail.empty() && now - trail.front().first > trail_duration_ns)
			trail.pop_front();
	}
	void render(QPainter &painter) override
	{
		const QRect heatmap = heatmap_rect();
		if (show_distance)
			draw_distance(painter);
		if (show_heatmap)
			draw_heatmap(painter, heatmap);
		if (show_live_mouse)
			draw_trail(painter, os_gettime_ns());
		draw_screen_guides(painter, heatmap);
		painter.setFont(font());
		if (show_live_mouse)
			draw_pointer(painter);
		if (show_coordinates && coordinates)
			draw_coordinates(painter, heatmap);
	}

private:
	struct motion_point {
		uint64_t time_ns;
		size_t hex_index;
	};
	struct hex_bin {
		QPointF center;
		uint64_t value{};
	};
	void resize_heatmap()
	{
		const QRect rect = heatmap_rect();
		if (rect == heatmap_bounds)
			return;
		heatmap_bounds = rect;
		build_hex_lattice();
		last_motion.reset();
	}
	void update_dimensions()
	{
		if (monitor.width <= 0 || monitor.height <= 0)
			return;
		const int content_width = std::max(1, width - padding * 2);
		const int heatmap_height = static_cast<int>(
			std::lround(content_width * static_cast<double>(monitor.height) / monitor.width));
		height = std::max(1, heatmap_height + padding * 2 + information_height() + title_content_offset());
	}
	QRect heatmap_rect() const
	{
		return {padding, padding + distance_height(), std::max(1, width - padding * 2),
			std::max(1, height - padding * 2 - information_height())};
	}
	int information_line_height() const { return QFontMetrics(font()).lineSpacing(); }
	int distance_height() const { return show_distance ? information_line_height() : 0; }
	int coordinates_height() const { return show_coordinates ? information_line_height() : 0; }
	int information_height() const { return distance_height() + coordinates_height(); }
	void build_hex_lattice()
	{
		hex_bins.clear();
		const QRect rect = heatmap_rect();
		const qreal hex_width = std::sqrt(3.0) * hex_radius;
		const qreal row_step = 1.5 * hex_radius;
		hex_columns = std::max(1, static_cast<int>(std::ceil(rect.width() / hex_width)) + 1);
		hex_rows = std::max(1, static_cast<int>(std::ceil(rect.height() / row_step)) + 1);
		hex_bins.reserve(static_cast<size_t>(hex_columns * hex_rows));
		for (int row = 0; row < hex_rows; ++row) {
			const qreal x_offset = row % 2 ? hex_width / 2.0 : 0.0;
			for (int column = 0; column < hex_columns; ++column)
				hex_bins.push_back({{rect.left() + hex_width / 2.0 + x_offset + column * hex_width,
						     rect.top() + hex_radius + row * row_step}});
		}
	}
	size_t nearest_hex(const QPointF &point) const
	{
		const QRect rect = heatmap_rect();
		const qreal hex_width = std::sqrt(3.0) * hex_radius;
		const qreal row_step = 1.5 * hex_radius;
		const int estimated_row =
			static_cast<int>(std::floor((point.y() - rect.top() - hex_radius) / row_step));
		size_t nearest{};
		qreal nearest_distance = std::numeric_limits<qreal>::max();
		for (int row = std::max(0, estimated_row - 1); row <= std::min(hex_rows - 1, estimated_row + 1);
		     ++row) {
			const qreal x_offset = row % 2 ? hex_width / 2.0 : 0.0;
			const int estimated_column = static_cast<int>(
				std::floor((point.x() - rect.left() - hex_width / 2.0 - x_offset) / hex_width));
			for (int column = std::max(0, estimated_column - 1);
			     column <= std::min(hex_columns - 1, estimated_column + 1); ++column) {
				const size_t index = static_cast<size_t>(row * hex_columns + column);
				const qreal dx = point.x() - hex_bins[index].center.x();
				const qreal dy = point.y() - hex_bins[index].center.y();
				const qreal distance = dx * dx + dy * dy;
				if (distance < nearest_distance) {
					nearest_distance = distance;
					nearest = index;
				}
			}
		}
		return nearest;
	}
	QPainterPath trail_path(size_t first, size_t last) const
	{
		QPainterPath path(trail[first].second);
		if (first == last)
			return path;
		if (first + 1 == last) {
			path.lineTo(trail[last].second);
			return path;
		}
		for (size_t index = first + 1; index < last; ++index) {
			const QPointF midpoint = (trail[index].second + trail[index + 1].second) / 2.0;
			path.quadTo(trail[index].second, midpoint);
		}
		path.lineTo(trail[last].second);
		return path;
	}
	void draw_trail(QPainter &painter, uint64_t now) const
	{
		if (trail.empty())
			return;
		painter.setBrush(Qt::NoBrush);
		size_t first_segment = 0;
		while (first_segment + 1 < trail.size()) {
			const double age = std::clamp(static_cast<double>(now - trail[first_segment + 1].first) /
							      trail_duration_ns,
						      0.0, 1.0);
			const int band = std::min(3, static_cast<int>(age * 4.0));
			size_t last_point = first_segment + 1;
			while (last_point + 1 < trail.size()) {
				const double next_age = std::clamp(
					static_cast<double>(now - trail[last_point + 1].first) / trail_duration_ns, 0.0,
					1.0);
				if (std::min(3, static_cast<int>(next_age * 4.0)) != band)
					break;
				++last_point;
			}
			const double strength = 1.0 - (band + 0.5) / 4.0;
			QPen pen(QColor(active_color.red(), active_color.green(), active_color.blue(),
					static_cast<int>(active_color.alpha() * 180.0 / 255.0 * strength * strength)));
			pen.setWidthF(2.0 + 6.0 * strength);
			pen.setCapStyle(Qt::FlatCap);
			pen.setJoinStyle(Qt::RoundJoin);
			painter.setPen(pen);
			painter.drawPath(trail_path(first_segment, last_point));
			first_segment = last_point;
		}
		painter.setBrush(active_color);
		painter.setPen(text_color);
		painter.drawEllipse(trail.back().second, 8, 8);
	}
	void draw_heatmap(QPainter &painter, const QRect &rect) const
	{
		std::vector<uint64_t> visited;
		visited.reserve(hex_bins.size());
		for (const auto &bin : hex_bins)
			if (bin.value)
				visited.push_back(bin.value);
		if (visited.empty())
			return;
		std::sort(visited.begin(), visited.end());
		const uint64_t first_quartile = visited[(visited.size() - 1) / 4];
		const uint64_t second_quartile = visited[(visited.size() - 1) / 2];
		const uint64_t third_quartile = visited[(visited.size() - 1) * 3 / 4];
		painter.save();
		painter.setClipRect(rect);
		for (const auto &bin : hex_bins) {
			if (!bin.value)
				continue;
			const int band = bin.value <= first_quartile    ? 0
					 : bin.value <= second_quartile ? 1
					 : bin.value <= third_quartile  ? 2
									: 3;
			QColor color = heatmap_color(band);
			color.setAlpha(150 * heatmap_opacity / 100);
			painter.setBrush(color);
			QColor outline = heatmap_color(band);
			outline.setAlpha(210 * heatmap_opacity / 100);
			QPen pen(outline, 0.75);
			pen.setJoinStyle(Qt::RoundJoin);
			painter.setPen(pen);
			QPolygonF hexagon;
			for (int corner = 0; corner < 6; ++corner) {
				const qreal angle = (30.0 + corner * 60.0) * M_PI / 180.0;
				hexagon << QPointF(bin.center.x() + hex_radius * std::cos(angle),
						   bin.center.y() + hex_radius * std::sin(angle));
			}
			painter.drawPolygon(hexagon);
		}
		painter.restore();
	}
	QColor heatmap_color(int band) const
	{
		if (heatmap_gradient == "lime") {
			const QColor colors[] = {{101, 163, 13}, {132, 204, 22}, {190, 242, 100}, {250, 204, 21}};
			return colors[band];
		}
		if (heatmap_gradient == "ocean") {
			const QColor colors[] = {{30, 64, 175}, {14, 116, 144}, {34, 197, 94}, {250, 204, 21}};
			return colors[band];
		}
		const QColor colors[] = {{59, 130, 246}, {6, 182, 212}, {250, 204, 21}, {239, 68, 68}};
		return colors[band];
	}
	void export_heatmap() const
	{
		const QRect rect = heatmap_rect();
		QDir directory(export_directory);
		if (!directory.exists() && !directory.mkpath("."))
			return;
		const QString base_name = QString("input-activity-heatmap-%1")
						  .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz"));
		const QString path = directory.filePath(base_name + (export_svg ? ".svg" : ".png"));
		if (export_svg) {
			QSvgGenerator generator;
			generator.setFileName(path);
			generator.setSize(rect.size());
			generator.setViewBox(QRect(QPoint(), rect.size()));
			QPainter painter(&generator);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.translate(-rect.topLeft());
			draw_heatmap(painter, rect);
		} else {
			QImage image(rect.size(), QImage::Format_RGBA8888);
			image.fill(Qt::transparent);
			QPainter painter(&image);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.translate(-rect.topLeft());
			draw_heatmap(painter, rect);
			image.save(path, "PNG");
		}
	}
	void load_display()
	{
		unsigned char count{};
		std::unique_ptr<screen_data, decltype(&free)> screens(hook_create_screen_info(&count), &free);
		if (screens && display >= 0 && display < count)
			monitor = screens.get()[display];
		else
			monitor = {};
	}
	void draw_screen_guides(QPainter &painter, const QRect &rect) const
	{
		if (!show_border && !show_center_mark)
			return;
		painter.save();
		QPen pen(text_color, 1.5);
		pen.setCapStyle(Qt::RoundCap);
		painter.setPen(pen);
		painter.setBrush(Qt::NoBrush);
		if (show_border)
			painter.drawRect(rect.adjusted(0, 0, -1, -1));
		if (show_center_mark) {
			const QPointF center = rect.center();
			constexpr qreal crosshair_radius = 8.0;
			painter.drawLine(center - QPointF(crosshair_radius, 0), center + QPointF(crosshair_radius, 0));
			painter.drawLine(center - QPointF(0, crosshair_radius), center + QPointF(0, crosshair_radius));
		}
		painter.restore();
	}
	void draw_coordinates(QPainter &painter, const QRect &heatmap) const
	{
		const QString label = QString("X: %1  Y: %2").arg(coordinates->x()).arg(coordinates->y());
		const int coordinates_y = heatmap.bottom() + 1;
		const QRect label_rect(padding, coordinates_y, std::max(1, width - padding * 2), coordinates_height());
		draw_text(painter, label_rect, information_alignment | Qt::AlignVCenter, label, text_color);
	}
	void draw_distance(QPainter &painter) const
	{
		const QRect label_rect(padding, padding, std::max(1, width - padding * 2), distance_height());
		draw_text(painter, label_rect, information_alignment | Qt::AlignVCenter,
			  QString("Distance: %1").arg(distance_label()), text_color);
	}
	QString distance_label() const
	{
		if (distance_unit == "metric") {
			double value = distance / mouse_dpi * 2.54;
			QString unit = "cm";
			if (value > 10000.0) {
				value /= 100.0;
				unit = "m";
				if (value > 10000.0) {
					value /= 1000.0;
					unit = "km";
				}
			}
			return QString("%1 %2").arg(value, 0, 'f', value < 10.0 ? 2 : 1).arg(unit);
		}
		if (distance_unit == "imperial") {
			double value = distance / mouse_dpi;
			QString unit = "in";
			if (value > 1200.0) {
				value /= 12.0;
				unit = "ft";
				if (value > 5280.0) {
					value /= 5280.0;
					unit = "mi";
				}
			}
			return QString("%1 %2").arg(value, 0, 'f', value < 10.0 ? 2 : 1).arg(unit);
		}
		return QString("%1 px").arg(distance, 0, 'f', 0);
	}
	void draw_pointer(QPainter &painter) const
	{
		if (!coordinates || monitor.width == 0 || monitor.height == 0)
			return;
		const QRect heatmap = heatmap_rect();
		const QPointF point(heatmap.x() + coordinates->x() * heatmap.width() / monitor.width,
				    heatmap.y() + coordinates->y() * heatmap.height() / monitor.height);
		const auto is_pressed = [&](uint16_t button) {
			const auto found = buttons.find(button);
			return found != buttons.end() && found->second;
		};
		const uint16_t highlighted_button = is_pressed(MOUSE_BUTTON1)   ? MOUSE_BUTTON1
						    : is_pressed(MOUSE_BUTTON2) ? MOUSE_BUTTON2
						    : is_pressed(MOUSE_BUTTON3) ? MOUSE_BUTTON3
										: 0;
		const QColor color = highlighted_button ? button_colors.at(highlighted_button) : active_color;
		QPainterPath pointer;
		pointer.moveTo(point);
		pointer.lineTo(point + QPointF(0, 22));
		pointer.lineTo(point + QPointF(6, 16));
		pointer.lineTo(point + QPointF(11, 27));
		pointer.lineTo(point + QPointF(16, 25));
		pointer.lineTo(point + QPointF(11, 14));
		pointer.lineTo(point + QPointF(19, 14));
		pointer.closeSubpath();
		painter.setBrush(color);
		painter.setPen(QPen(text_color, 1.5));
		painter.drawPath(pointer);
		if (!highlighted_button)
			return;
		const QString &label = highlighted_button == MOUSE_BUTTON1   ? left_label
				       : highlighted_button == MOUSE_BUTTON2 ? right_label
									     : middle_label;
		if (label.isEmpty())
			return;
		const QFontMetrics metrics(painter.font());
		const QRect label_rect(static_cast<int>(point.x()) - metrics.horizontalAdvance(label) - 4,
				       static_cast<int>(point.y()) - metrics.height() - 4,
				       std::max(1, metrics.horizontalAdvance(label) + 2),
				       std::max(1, metrics.height() + 2));
		draw_text(painter, label_rect, Qt::AlignCenter, label, color);
	}
	QString left_label{"L"}, right_label{"R"}, middle_label{"M"};
	QColor active_color{37, 99, 235};
	std::unordered_map<uint16_t, QColor> button_colors{{MOUSE_BUTTON1, {37, 99, 235}},
							   {MOUSE_BUTTON2, {239, 68, 68}},
							   {MOUSE_BUTTON3, {250, 204, 21}}};
	bool show_coordinates{}, show_heatmap{true}, show_live_mouse{true}, show_distance{}, show_border{},
		show_center_mark{}, map_clicks{};
	Qt::Alignment information_alignment{Qt::AlignLeft};
	std::string heatmap_gradient{"spectrum"};
	uint64_t trail_duration_ns{1500ULL * 1000 * 1000};
	qreal hex_radius{default_heatmap_hex_radius};
	int heatmap_opacity{100};
	QString export_directory;
	bool export_svg{};
	obs_hotkey_id export_hotkey = OBS_INVALID_HOTKEY_ID;
	int display{};
	int64_t mouse_dpi{800};
	QString distance_unit{"pixels"};
	double distance{};
	screen_data monitor{};
	std::unordered_map<uint16_t, bool> buttons;
	std::deque<std::pair<uint64_t, QPoint>> trail;
	std::optional<QPoint> coordinates;
	std::optional<QPoint> last_distance;
	std::optional<motion_point> last_motion;
	QRect heatmap_bounds;
	int hex_columns{}, hex_rows{};
	std::vector<hex_bin> hex_bins;
};

class statistics_source final : public activity_source {
public:
	statistics_source(obs_source_t *source, obs_data_t *settings, bool register_hotkeys = true,
			  bool initial_update = true)
		: activity_source(source, settings, register_hotkeys, initial_update)
	{
	}
	void update(obs_data_t *settings) override
	{
		activity_source::update(settings);
		show_key_rate = obs_data_get_bool(settings, "statistics.show_key_rate");
		show_total_keys = obs_data_get_bool(settings, "statistics.show_total_keys");
		show_click_rate = obs_data_get_bool(settings, "statistics.show_click_rate");
		show_total_clicks = obs_data_get_bool(settings, "statistics.show_total_clicks");
		show_action_rate = obs_data_get_bool(settings, "statistics.show_action_rate");
		show_total_actions = obs_data_get_bool(settings, "statistics.show_total_actions");
		element_spacing =
			std::clamp(static_cast<int>(obs_data_get_int(settings, "statistics.element_spacing")), 0, 200);
		show_lap_keys = obs_data_get_bool(settings, "statistics.show_lap_keys");
		show_lap_clicks = obs_data_get_bool(settings, "statistics.show_lap_clicks");
		show_lap_actions = obs_data_get_bool(settings, "statistics.show_lap_actions");
	}
	void on_event(const input_data::trace_event &event) override
	{
		if (event.type == EVENT_KEY_PRESSED) {
			if (!held_keys[event.code]) {
				held_keys[event.code] = true;
				keys.push_back(event.time_ns);
				++total_keys;
				++lap_keys;
			}
		} else if (event.type == EVENT_KEY_RELEASED) {
			held_keys[event.code] = false;
		} else if (event.type == EVENT_MOUSE_PRESSED && event.code >= MOUSE_BUTTON1 &&
			   event.code <= MOUSE_BUTTON3) {
			if (!held_buttons[event.code]) {
				held_buttons[event.code] = true;
				clicks.push_back(event.time_ns);
				++total_clicks;
				++lap_clicks;
			}
		} else if (event.type == EVENT_MOUSE_RELEASED) {
			held_buttons[event.code] = false;
		}
	}
	void on_snapshot(const input_data::button_map<uint16_t> &keyboard,
			 const input_data::button_map<uint16_t> &mouse) override
	{
		held_keys = keyboard;
		for (uint16_t button = MOUSE_BUTTON1; button <= MOUSE_BUTTON3; ++button) {
			const auto pressed = mouse.find(button);
			held_buttons[button] = pressed != mouse.end() && pressed->second;
		}
	}
	void tick(float seconds) override
	{
		activity_source::tick(seconds);
		const uint64_t cutoff = os_gettime_ns() - minute_ns;
		while (!keys.empty() && keys.front() < cutoff)
			keys.pop_front();
		while (!clicks.empty() && clicks.front() < cutoff)
			clicks.pop_front();
	}
	void render(QPainter &painter) override
	{
		painter.setFont(font());
		QStringList lines;
		QStringList key_metrics;
		if (show_key_rate)
			key_metrics.append(QString("KPM: %1").arg(keys.size()));
		if (show_total_keys)
			key_metrics.append(QString("Total keys: %1").arg(total_keys));
		if (!key_metrics.isEmpty())
			lines.append(key_metrics.join("  "));

		QStringList click_metrics;
		if (show_click_rate)
			click_metrics.append(QString("CPM: %1").arg(clicks.size()));
		if (show_total_clicks)
			click_metrics.append(QString("Total clicks: %1").arg(total_clicks));
		if (!click_metrics.isEmpty())
			lines.append(click_metrics.join("  "));

		QStringList action_metrics;
		if (show_action_rate)
			action_metrics.append(QString("APM: %1").arg(keys.size() + clicks.size()));
		if (show_total_actions)
			action_metrics.append(QString("Total actions: %1").arg(total_keys + total_clicks));
		if (!action_metrics.isEmpty())
			lines.append(action_metrics.join("  "));

		QStringList lap_metrics;
		if (show_lap_keys)
			lap_metrics.append(QString("%1: %2").arg(obs_module_text("Statistics.LapKeys")).arg(lap_keys));
		if (show_lap_clicks)
			lap_metrics.append(
				QString("%1: %2").arg(obs_module_text("Statistics.LapClicks")).arg(lap_clicks));
		if (show_lap_actions)
			lap_metrics.append(QString("%1: %2")
						   .arg(obs_module_text("Statistics.LapActions"))
						   .arg(lap_keys + lap_clicks));
		if (!lap_metrics.isEmpty())
			lines.append(lap_metrics.join("  "));

		const QRect bounds(padding, padding, width - padding * 2, height - padding * 2);
		if (lines.isEmpty()) {
			draw_text(painter, bounds, Qt::AlignCenter, obs_module_text("Statistics.NoMetrics"),
				  text_color);
			return;
		}

		const QFontMetrics metrics(font());
		const int line_height = metrics.lineSpacing();
		const int gap = std::min(element_spacing,
					 std::max(0, (bounds.height() - static_cast<int>(lines.size()) * line_height) /
							     std::max(1, static_cast<int>(lines.size()) - 1)));
		const int total_height = static_cast<int>(lines.size()) * line_height +
					 std::max(0, static_cast<int>(lines.size()) - 1) * gap;
		int top = bounds.center().y() - total_height / 2;
		for (const QString &line : lines) {
			draw_text(painter, QRect(bounds.left(), top, bounds.width(), line_height),
				  Qt::AlignLeft | Qt::AlignVCenter, line, text_color);
			top += line_height + gap;
		}
	}
	void reset_activity() override
	{
		keys.clear();
		clicks.clear();
		total_keys = 0;
		total_clicks = 0;
		lap_keys = 0;
		lap_clicks = 0;
	}
	void lap_activity() override
	{
		lap_keys = 0;
		lap_clicks = 0;
	}

private:
	std::deque<uint64_t> keys, clicks;
	std::unordered_map<uint16_t, bool> held_keys, held_buttons;
	uint64_t total_keys{}, total_clicks{}, lap_keys{}, lap_clicks{};
	int element_spacing{};
	bool show_key_rate{true}, show_total_keys{true}, show_click_rate{true}, show_total_clicks{true};
	bool show_action_rate{true}, show_total_actions{true};
	bool show_lap_keys{}, show_lap_clicks{}, show_lap_actions{};
};

enum class intensity_metric { keyboard, mouse, actions, key, button, velocity };
enum class intensity_key_scope { all, letters, numbers, list };

struct intensity_row {
	bool enabled{};
	intensity_metric metric{intensity_metric::actions};
	uint16_t key{};
	uint16_t button{MOUSE_BUTTON1};
	intensity_key_scope key_scope{intensity_key_scope::all};
	QString title;
	QString key_list;

	bool operator==(const intensity_row &other) const
	{
		return enabled == other.enabled && metric == other.metric && key == other.key &&
		       button == other.button && key_scope == other.key_scope && title == other.title &&
		       key_list == other.key_list;
	}
	bool operator!=(const intensity_row &other) const { return !(*this == other); }
};

class input_intensity_source final : public activity_source {
public:
	using sample = std::array<double, 8>;

	input_intensity_source(obs_source_t *source, obs_data_t *settings, bool register_hotkeys = true,
			       bool initial_update = true)
		: activity_source(source, settings, register_hotkeys, initial_update)
	{
	}

	void update(obs_data_t *settings) override
	{
		activity_source::update(settings);
		migrate_legacy_colors(settings, "input_intensity.colors_with_alpha", {"input_intensity.color"});

		const int new_window =
			std::clamp(static_cast<int>(obs_data_get_int(settings, "input_intensity.window")), 1, 60);
		element_spacing = std::clamp(
			static_cast<int>(obs_data_get_int(settings, "input_intensity.element_spacing")), 0, 200);
		std::array<intensity_row, 8> new_rows{};
		for (size_t index = 0; index < new_rows.size(); ++index) {
			const std::string prefix = "input_intensity.row" + std::to_string(index) + ".";
			new_rows[index].enabled = obs_data_get_bool(settings, (prefix + "enabled").c_str());
			const std::string type = obs_data_get_string(settings, (prefix + "metric").c_str());
			if (type == "keyboard")
				new_rows[index].metric = intensity_metric::keyboard;
			else if (type == "mouse")
				new_rows[index].metric = intensity_metric::mouse;
			else if (type == "key")
				new_rows[index].metric = intensity_metric::key;
			else if (type == "button")
				new_rows[index].metric = intensity_metric::button;
			else if (type == "velocity")
				new_rows[index].metric = intensity_metric::velocity;
			else
				new_rows[index].metric = intensity_metric::actions;
			new_rows[index].key =
				static_cast<uint16_t>(obs_data_get_int(settings, (prefix + "key").c_str()));
			new_rows[index].button =
				static_cast<uint16_t>(obs_data_get_int(settings, (prefix + "button").c_str()));
			const std::string scope = obs_data_get_string(settings, (prefix + "key_scope").c_str());
			new_rows[index].key_scope = scope == "letters"   ? intensity_key_scope::letters
						    : scope == "numbers" ? intensity_key_scope::numbers
						    : scope == "list"    ? intensity_key_scope::list
									 : intensity_key_scope::all;
			new_rows[index].title =
				QString::fromUtf8(obs_data_get_string(settings, (prefix + "title").c_str()));
			new_rows[index].key_list =
				QString::fromUtf8(obs_data_get_string(settings, (prefix + "key_list").c_str()));
		}
		const QColor new_color =
			obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "input_intensity.color")));
		const bool data_changed = configured && (new_window != window_seconds || new_rows != rows);
		window_seconds = new_window;
		rows = new_rows;
		accent_color = new_color;
		if (data_changed)
			clear_samples();
		configured = true;
	}

	void on_event(const input_data::trace_event &event) override
	{
		advance_to(event.time_ns);
		if (event.type == EVENT_KEY_PRESSED) {
			if (!held_keys[event.code]) {
				held_keys[event.code] = true;
				for_each_matching(
					[&](const intensity_row &row) {
						return row.metric == intensity_metric::keyboard ||
						       row.metric == intensity_metric::actions ||
						       (row.metric == intensity_metric::key && row.key == event.code);
					},
					1.0, event.code);
			}
		} else if (event.type == EVENT_KEY_RELEASED) {
			held_keys[event.code] = false;
		} else if (event.type == EVENT_MOUSE_PRESSED && event.code >= MOUSE_BUTTON1 &&
			   event.code <= MOUSE_BUTTON5) {
			if (!held_buttons[event.code]) {
				held_buttons[event.code] = true;
				for_each_matching([&](const intensity_row &row) {
					return row.metric == intensity_metric::mouse ||
					       row.metric == intensity_metric::actions ||
					       (row.metric == intensity_metric::button && row.button == event.code);
				});
			}
		} else if (event.type == EVENT_MOUSE_RELEASED) {
			held_buttons[event.code] = false;
		} else if (event.type == EVENT_MOUSE_MOVED || event.type == EVENT_MOUSE_DRAGGED) {
			if (last_motion) {
				const double distance = std::hypot(static_cast<double>(event.x - last_motion->x),
								   static_cast<double>(event.y - last_motion->y));
				for_each_matching(
					[&](const intensity_row &row) {
						return row.metric == intensity_metric::velocity;
					},
					distance);
			}
			last_motion = event;
		}
	}

	void on_snapshot(const input_data::button_map<uint16_t> &keyboard,
			 const input_data::button_map<uint16_t> &mouse) override
	{
		held_keys = keyboard;
		held_buttons = mouse;
	}

	void tick(float seconds) override
	{
		activity_source::tick(seconds);
		advance_to(os_gettime_ns());
	}

	void reset_activity() override { clear_samples(); }

	void render(QPainter &painter) override
	{
		std::vector<size_t> active_rows;
		for (size_t index = 0; index < rows.size(); ++index)
			if (rows[index].enabled)
				active_rows.push_back(index);
		if (active_rows.empty()) {
			painter.setFont(font());
			draw_text(painter, QRect(padding, padding, width - padding * 2, height - padding * 2),
				  Qt::AlignCenter, obs_module_text("InputIntensity.NoMetrics"), text_color);
			return;
		}

		const QRect bounds(padding, padding, std::max(1, width - padding * 2),
				   std::max(1, height - padding * 2));
		const int gap = std::min(element_spacing,
					 std::max(0, (bounds.height() - static_cast<int>(active_rows.size())) /
							     std::max(1, static_cast<int>(active_rows.size()) - 1)));
		const int total_spacing = gap * std::max(0, static_cast<int>(active_rows.size()) - 1);
		const int row_height =
			std::max(1, (bounds.height() - total_spacing) / static_cast<int>(active_rows.size()));
		QFont row_font = font();
		row_font.setPixelSize(std::clamp(row_height / 3, 9, font_size));
		painter.setFont(row_font);
		const int text_height = QFontMetrics(row_font).lineSpacing() + 4;

		for (size_t visible_index = 0; visible_index < active_rows.size(); ++visible_index) {
			const size_t row_index = active_rows[visible_index];
			const QRect row_rect(bounds.left(),
					     bounds.top() + static_cast<int>(visible_index) * (row_height + gap),
					     bounds.width(), row_height);
			const int label_height = std::max(1, std::min(row_rect.height() / 3, text_height));
			const int value_label_height = std::max(1, std::min(row_rect.height() / 3, text_height));
			const QRect label_rect(row_rect.left(), row_rect.top() + 1, row_rect.width(), label_height - 1);
			const QRect chart_rect(row_rect.left(), label_rect.bottom() + 3, row_rect.width(),
					       std::max(1, row_rect.height() - label_height - value_label_height - 4));
			const QRect value_label_rect(row_rect.left(), chart_rect.bottom() + 1, row_rect.width(),
						     value_label_height + 1);
			draw_text(painter, label_rect, Qt::AlignLeft | Qt::AlignTop, row_label(rows[row_index]),
				  text_color);
			draw_box_plot(painter, chart_rect, value_label_rect, row_index);
		}
	}

private:
	template<typename Predicate>
	void for_each_matching(Predicate predicate, double amount = 1.0, uint16_t key_code = 0)
	{
		for (size_t index = 0; index < rows.size(); ++index)
			if (rows[index].enabled && predicate(rows[index]) &&
			    (key_code == 0 || rows[index].metric != intensity_metric::keyboard ||
			     matches_key_scope(rows[index], key_code)))
				current[index] += amount;
	}
	static bool matches_key_scope(const intensity_row &row, uint16_t key_code)
	{
		switch (row.key_scope) {
		case intensity_key_scope::all:
			return true;
		case intensity_key_scope::letters:
			return key_code >= VC_A && key_code <= VC_Z;
		case intensity_key_scope::numbers:
			return key_code >= VC_0 && key_code <= VC_9;
		case intensity_key_scope::list:
			return key_list_contains(row.key_list, key_code);
		}
		return false;
	}
	static bool key_list_contains(const QString &list, uint16_t key_code)
	{
		for (QString token : list.split(',', Qt::SkipEmptyParts)) {
			token = token.trimmed().toLower();
			if ((token.size() == 1 && token[0].isLetter() && key_code == VC_A + token[0].unicode() - 'a') ||
			    (token.size() == 1 && token[0].isDigit() && key_code == VC_0 + token[0].unicode() - '0') ||
			    (token == "space" && key_code == VC_SPACE) || (token == "return" && key_code == VC_ENTER) ||
			    (token == "enter" && key_code == VC_ENTER) || (token == "tab" && key_code == VC_TAB) ||
			    (token == "esc" && key_code == VC_ESCAPE) || (token == "escape" && key_code == VC_ESCAPE) ||
			    (token == "delete" && key_code == VC_BACKSPACE) ||
			    (token == "del" && key_code == VC_BACKSPACE))
				return true;
		}
		return false;
	}

	void advance_to(uint64_t now)
	{
		if (now == 0)
			return;
		if (bucket_start_ns == 0) {
			bucket_start_ns = now;
			return;
		}
		if (now <= bucket_start_ns)
			return;
		while (now - bucket_start_ns >= second_ns) {
			samples.push_back(current);
			if (samples.size() > 60)
				samples.pop_front();
			current.fill(0.0);
			bucket_start_ns += second_ns;
		}
	}

	void clear_samples()
	{
		samples.clear();
		current.fill(0.0);
		bucket_start_ns = 0;
		held_keys.clear();
		held_buttons.clear();
		last_motion.reset();
	}

	QString row_label(const intensity_row &row) const
	{
		if (!row.title.isEmpty())
			return row.title;
		switch (row.metric) {
		case intensity_metric::keyboard:
			return QString("%1 (/s)").arg(obs_module_text("InputIntensity.Metric.Keyboard"));
		case intensity_metric::mouse:
			return QString("%1 (/s)").arg(obs_module_text("InputIntensity.Metric.Mouse"));
		case intensity_metric::actions:
			return QString("%1 (/s)").arg(obs_module_text("InputIntensity.Metric.Actions"));
		case intensity_metric::key: {
			input_data::trace_event event{};
			event.code = row.key;
			return QString("%1 (/s)").arg(key_name(event));
		}
		case intensity_metric::button:
			return QString("%1 %2 (/s)")
				.arg(obs_module_text("InputIntensity.Metric.MouseButton"))
				.arg(row.button);
		case intensity_metric::velocity:
			return QString("%1 (px/s)").arg(obs_module_text("InputIntensity.Metric.Velocity"));
		}
		return {};
	}

	void draw_box_plot(QPainter &painter, const QRect &rect, const QRect &value_label_rect, size_t row_index) const
	{
		std::vector<double> values;
		const size_t first = samples.size() > static_cast<size_t>(window_seconds)
					     ? samples.size() - static_cast<size_t>(window_seconds)
					     : 0;
		values.reserve(std::max<size_t>(1, samples.size() - first));
		for (size_t index = first; index < samples.size(); ++index)
			values.push_back(samples[index][row_index]);
		if (values.empty())
			values.push_back(0.0);
		std::sort(values.begin(), values.end());
		const double minimum = values.front();
		const double maximum = values.back();
		const double first_quartile = values[(values.size() - 1) / 4];
		const double median = values[(values.size() - 1) / 2];
		const double third_quartile = values[(values.size() - 1) * 3 / 4];
		const double current_value = current_rate(row_index);
		const double scale_minimum = std::min(minimum, current_value);
		const double scale_maximum = std::max(maximum, current_value);
		const double range = scale_maximum - scale_minimum;
		const auto position = [&](double value) {
			if (range == 0.0)
				return rect.center().x();
			return rect.left() +
			       static_cast<int>(std::lround((value - scale_minimum) / range * (rect.width() - 1)));
		};
		const int min_x = position(minimum);
		const int max_x = position(maximum);
		const int q1_x = position(first_quartile);
		const int q3_x = position(third_quartile);
		const int median_x = position(median);
		const int current_x = position(current_value);
		const int center_y = rect.center().y();
		const int box_height = std::max(6, rect.height() / 2);
		const QRect box(std::min(q1_x, q3_x), center_y - box_height / 2, std::max(1, std::abs(q3_x - q1_x)),
				box_height);

		QPen whisker(text_color, 1.0);
		painter.setPen(whisker);
		painter.setBrush(Qt::NoBrush);
		painter.drawLine(min_x, center_y, max_x, center_y);
		painter.drawLine(min_x, center_y - 3, min_x, center_y + 3);
		painter.drawLine(max_x, center_y - 3, max_x, center_y + 3);
		QColor fill = accent_color;
		fill.setAlpha(150);
		painter.setBrush(fill);
		painter.drawRect(box);
		QPen median_pen(text_color, 2.0);
		painter.setPen(median_pen);
		painter.drawLine(median_x, box.top(), median_x, box.bottom());

		QPen current_pen(accent_color, 2.0);
		painter.setPen(current_pen);
		painter.drawLine(current_x, rect.top(), current_x, rect.bottom());
		painter.setBrush(accent_color);
		painter.drawEllipse(QPoint(current_x, center_y), 3, 3);

		const QString min_label = number_label(minimum);
		const QString max_label = number_label(maximum);
		draw_text(painter, value_label_rect, Qt::AlignLeft | Qt::AlignVCenter, min_label, text_color);
		draw_text(painter, value_label_rect, Qt::AlignRight | Qt::AlignVCenter, max_label, text_color);
	}

	double current_rate(size_t row_index) const
	{
		if (bucket_start_ns == 0)
			return 0.0;
		const uint64_t now = os_gettime_ns();
		if (now <= bucket_start_ns)
			return 0.0;
		const uint64_t elapsed_ns = std::min(second_ns, now - bucket_start_ns);
		if (elapsed_ns == 0)
			return 0.0;
		return current[row_index] * static_cast<double>(second_ns) / static_cast<double>(elapsed_ns);
	}

	static QString number_label(double value) { return QString::number(value, 'f', value < 10.0 ? 1 : 0); }

	static constexpr uint64_t second_ns = 1000ULL * 1000 * 1000;
	int window_seconds{30};
	std::array<intensity_row, 8> rows{};
	std::deque<sample> samples;
	sample current{};
	std::unordered_map<uint16_t, bool> held_keys, held_buttons;
	std::optional<input_data::trace_event> last_motion;
	QColor accent_color{37, 99, 235};
	int element_spacing{};
	uint64_t bucket_start_ns{};
	bool configured{};
};

class unified_source final {
public:
	unified_source(obs_source_t *source, obs_data_t *settings) : source(source)
	{
		common_settings = obs_data_create();
		copy_shared_settings(common_settings, settings);
		for (auto &saved : mode_settings) {
			saved = obs_data_create();
			obs_data_apply(saved, settings);
		}
		obs_data_set_string(mode_settings[mode_index(source_mode::live_keys)], "activity.title", "Live Keys");
		obs_data_set_bool(mode_settings[mode_index(source_mode::live_keys)], "activity.show_title", false);
		obs_data_set_string(mode_settings[mode_index(source_mode::mouse_activity)], "activity.title",
				    "Mouse Activity");
		obs_data_set_string(mode_settings[mode_index(source_mode::input_intensity)], "activity.title",
				    "Input Intensity");
		obs_data_set_string(mode_settings[mode_index(source_mode::input_statistics)], "activity.title",
				    "Input Statistics");
		live_keys = std::make_unique<live_keys_source>(source, settings, false, false);
		mouse_activity = std::make_unique<mouse_activity_source>(source, settings, false, false);
		input_intensity = std::make_unique<input_intensity_source>(source, settings, false, false);
		statistics = std::make_unique<statistics_source>(source, settings, false, false);
		reset_hotkey = obs_hotkey_register_source(
			source, "reset_input_activity", obs_module_text("Activity.ResetHotkey"),
			[](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
				if (pressed)
					static_cast<unified_source *>(data)->active()->reset_activity();
			},
			this);
		lap_hotkey = obs_hotkey_register_source(
			source, "lap_input_activity", obs_module_text("Activity.LapHotkey"),
			[](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
				if (pressed)
					static_cast<unified_source *>(data)->active()->lap_activity();
			},
			this);
		export_hotkey = obs_hotkey_register_source(
			source, "export_mouse_heatmap", obs_module_text("MouseActivity.ExportHotkey"),
			[](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
				auto *unified = static_cast<unified_source *>(data);
				if (pressed && unified->mode == source_mode::mouse_activity)
					unified->mouse_activity->export_current_heatmap();
			},
			this);
		update(settings);
	}
	~unified_source()
	{
		obs_hotkey_unregister(reset_hotkey);
		obs_hotkey_unregister(lap_hotkey);
		obs_hotkey_unregister(export_hotkey);
		for (auto *saved : mode_settings)
			obs_data_release(saved);
		obs_data_release(common_settings);
	}
	void update(obs_data_t *settings)
	{
		const std::string selected = obs_data_get_string(settings, "input_activity.mode");
		const source_mode selected_mode = selected == "mouse_activity"     ? source_mode::mouse_activity
						  : selected == "input_intensity"  ? source_mode::input_intensity
						  : selected == "input_statistics" ? source_mode::input_statistics
										   : source_mode::live_keys;
		if (configured) {
			obs_data_apply(mode_settings[mode_index(mode)], settings);
			copy_shared_settings(common_settings, settings);
		}
		if (configured && selected_mode != mode) {
			obs_data_apply(settings, mode_settings[mode_index(selected_mode)]);
			copy_shared_settings(settings, common_settings);
			obs_data_set_string(settings, "input_activity.mode", selected.c_str());
		}
		mode = selected_mode;
		configured = true;
		active()->update(settings);
	}
	void tick(float seconds) { active()->tick(seconds); }
	void draw(gs_effect_t *effect) { active()->draw(effect); }
	uint32_t width() const { return static_cast<uint32_t>(active()->width); }
	uint32_t height() const { return static_cast<uint32_t>(active()->height); }
	void clear_mouse_activity() { mouse_activity->clear(); }
	mouse_activity_source *mouse() const { return mouse_activity.get(); }

private:
	enum class source_mode { live_keys, mouse_activity, input_intensity, input_statistics };
	static size_t mode_index(source_mode value) { return static_cast<size_t>(value); }
	static void copy_shared_settings(obs_data_t *destination, obs_data_t *source)
	{
		const char *const integer_keys[] = {"activity.width",
						    "activity.height",
						    "activity.padding",
						    "activity.font_size",
						    "activity.text_color",
						    "activity.background_color",
						    "activity.text_shadow_color",
						    "activity.text_shadow_offset",
						    "activity.target.display"};
		for (const char *key : integer_keys)
			obs_data_set_int(destination, key, obs_data_get_int(source, key));
		obs_data_set_bool(destination, "activity.text_shadow",
				  obs_data_get_bool(source, "activity.text_shadow"));
		const char *const string_keys[] = {"activity.target.type", "activity.target.application",
						   "activity.target.window"};
		for (const char *key : string_keys)
			obs_data_set_string(destination, key, obs_data_get_string(source, key));
		if (auto *font = obs_data_get_obj(source, "activity.font")) {
			obs_data_set_obj(destination, "activity.font", font);
			obs_data_release(font);
		}
	}
	activity_source *active() const
	{
		switch (mode) {
		case source_mode::mouse_activity:
			return mouse_activity.get();
		case source_mode::input_intensity:
			return input_intensity.get();
		case source_mode::input_statistics:
			return statistics.get();
		case source_mode::live_keys:
			return live_keys.get();
		}
		return live_keys.get();
	}
	obs_source_t *source{};
	obs_hotkey_id reset_hotkey = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id lap_hotkey = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id export_hotkey = OBS_INVALID_HOTKEY_ID;
	source_mode mode{source_mode::live_keys};
	std::unique_ptr<live_keys_source> live_keys;
	std::unique_ptr<mouse_activity_source> mouse_activity;
	std::unique_ptr<input_intensity_source> input_intensity;
	std::unique_ptr<statistics_source> statistics;
	std::array<obs_data_t *, 4> mode_settings{};
	obs_data_t *common_settings{};
	bool configured{};
};

bool target_type_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const std::string target_type = obs_data_get_string(settings, "activity.target.type");
	obs_property_set_visible(obs_properties_get(props, "activity.target.display"), target_type == "display");
	obs_property_set_visible(obs_properties_get(props, "activity.target.application"),
				 target_type == "application");
	obs_property_set_visible(obs_properties_get(props, "activity.target.window"), target_type == "window");
	return true;
}

void add_common_properties(obs_properties_t *props, bool allow_height = true)
{
	auto *target_type = obs_properties_add_list(props, "activity.target.type", obs_module_text("Activity.Target"),
						    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(target_type, obs_module_text("Activity.Target.All"), "all");
	obs_property_list_add_string(target_type, obs_module_text("Activity.Target.Display"), "display");
	obs_property_list_add_string(target_type, obs_module_text("Activity.Target.Application"), "application");
	obs_property_list_add_string(target_type, obs_module_text("Activity.Target.Window"), "window");
	obs_property_set_modified_callback(target_type, target_type_changed);
	auto *target_display = obs_properties_add_list(props, "activity.target.display",
						       obs_module_text("Activity.Target.Display"), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_INT);
	for (const auto &display : uiohook::target_displays())
		obs_property_list_add_int(target_display, display.label.c_str(), display.id);
	auto *target_application = obs_properties_add_list(props, "activity.target.application",
							   obs_module_text("Activity.Target.Application"),
							   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const auto &application : uiohook::target_applications())
		obs_property_list_add_string(target_application, application.label.c_str(), application.id.c_str());
	auto *target_window = obs_properties_add_list(props, "activity.target.window",
						      obs_module_text("Activity.Target.Window"), OBS_COMBO_TYPE_LIST,
						      OBS_COMBO_FORMAT_STRING);
	for (const auto &window : uiohook::target_windows()) {
		const std::string id = window.application_id + "#" + std::to_string(window.id);
		obs_property_list_add_string(target_window, window.label.c_str(), id.c_str());
	}
	obs_property_set_visible(target_display, false);
	obs_property_set_visible(target_application, false);
	obs_property_set_visible(target_window, false);
	obs_properties_add_int(props, "activity.width", obs_module_text("Activity.Width"), 64, 3840, 1);
	if (allow_height)
		obs_properties_add_int(props, "activity.height", obs_module_text("Activity.Height"), 32, 2160, 1);
	obs_properties_add_int(props, "activity.padding", obs_module_text("Activity.Padding"), 0, 200, 1);
	obs_properties_add_font(props, "activity.font", obs_module_text("Activity.Font"));
	obs_properties_add_int(props, "activity.font_size", obs_module_text("Activity.FontSize"), 8, 256, 1);
	obs_properties_add_color_alpha(props, "activity.text_color", obs_module_text("Activity.TextColor"));
	obs_properties_add_color_alpha(props, "activity.background_color", obs_module_text("Activity.BackgroundColor"));
	obs_properties_add_bool(props, "activity.text_shadow", obs_module_text("Activity.TextShadow"));
	obs_properties_add_color_alpha(props, "activity.text_shadow_color",
				       obs_module_text("Activity.TextShadowColor"));
	obs_properties_add_int_slider(props, "activity.text_shadow_offset",
				      obs_module_text("Activity.TextShadowOffset"), 0, 20, 1);
}

void add_mode_title_properties(obs_properties_t *props)
{
	obs_properties_add_bool(props, "activity.show_title", obs_module_text("Activity.ShowTitle"));
	obs_properties_add_text(props, "activity.title", obs_module_text("Activity.Title"), OBS_TEXT_DEFAULT);
}
template<typename T> void register_source(const char *id, obs_properties_t *(*properties)(void *))
{
	obs_source_info info{};
	info.id = id;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO;
	if constexpr (std::is_same_v<T, live_keys_source>)
		info.get_name = [](void *) {
			return obs_module_text("LiveKeys");
		};
	else if constexpr (std::is_same_v<T, mouse_activity_source>)
		info.get_name = [](void *) {
			return obs_module_text("MouseActivity");
		};
	else if constexpr (std::is_same_v<T, input_intensity_source>)
		info.get_name = [](void *) {
			return obs_module_text("InputIntensity");
		};
	else if constexpr (std::is_same_v<T, unified_source>)
		info.get_name = [](void *) {
			return obs_module_text("InputActivity");
		};
	else
		info.get_name = [](void *) {
			return obs_module_text("InputStatistics");
		};
	info.create = [](obs_data_t *settings, obs_source_t *source) {
		return static_cast<void *>(new T(source, settings));
	};
	info.destroy = [](void *data) {
		delete static_cast<T *>(data);
	};
	info.update = [](void *data, obs_data_t *settings) {
		static_cast<T *>(data)->update(settings);
	};
	info.video_tick = [](void *data, float seconds) {
		static_cast<T *>(data)->tick(seconds);
	};
	info.video_render = [](void *data, gs_effect_t *effect) {
		static_cast<T *>(data)->draw(effect);
	};
	info.get_width = [](void *data) {
		if constexpr (std::is_same_v<T, unified_source>)
			return static_cast<T *>(data)->width();
		else
			return static_cast<uint32_t>(static_cast<T *>(data)->width);
	};
	info.get_height = [](void *data) {
		if constexpr (std::is_same_v<T, unified_source>)
			return static_cast<T *>(data)->height();
		else
			return static_cast<uint32_t>(static_cast<T *>(data)->height);
	};
	info.get_properties = properties;
	info.get_defaults = [](obs_data_t *settings) {
		obs_data_set_default_int(settings, "activity.width", 640);
		obs_data_set_default_int(settings, "activity.height", 240);
		obs_data_set_default_int(settings, "activity.padding", 20);
		obs_data_set_default_int(settings, "activity.font_size", 36);
		obs_data_set_default_int(settings, "activity.text_color", 0xffffffff);
		obs_data_set_default_int(settings, "activity.background_color", 0x00000000);
		obs_data_set_default_bool(settings, "activity.text_shadow", false);
		obs_data_set_default_int(settings, "activity.text_shadow_color", 0xcc000000);
		obs_data_set_default_int(settings, "activity.text_shadow_offset", 2);
		obs_data_set_default_bool(settings, "activity.show_title", true);
		obs_data_set_default_string(settings, "activity.target.type", "all");
		if constexpr (std::is_same_v<T, unified_source>) {
			obs_data_set_default_string(settings, "input_activity.mode", "live_keys");
			obs_data_set_default_string(settings, "activity.title", "Live Keys");
			obs_data_set_default_bool(settings, "activity.show_title", false);
			obs_data_set_default_int(settings, "live_keys.maximum", 8);
			obs_data_set_default_int(settings, "live_keys.top_n", 8);
			obs_data_set_default_int(settings, "live_keys.row_height", 96);
			obs_data_set_default_bool(settings, "live_keys.row_layout", true);
			obs_data_set_default_string(settings, "live_keys.top_n_alignment", "left");
			obs_data_set_default_int(settings, "live_keys.element_spacing", 10);
			obs_data_set_default_bool(settings, "live_keys.show_live_title", true);
			obs_data_set_default_string(settings, "live_keys.live_title", "Live Keys");
			obs_data_set_default_int(settings, "live_keys.live_title_font_size", 28);
			obs_data_set_default_bool(settings, "live_keys.show_most_used_title", true);
			obs_data_set_default_string(settings, "live_keys.most_used_title", "Most Used Keys");
			obs_data_set_default_int(settings, "live_keys.most_used_title_font_size", 28);
			obs_data_set_default_int(settings, "live_keys.key_font_size", 36);
			obs_data_set_default_int(settings, "live_keys.special_key_font_size", 28);
			obs_data_set_default_int(settings, "live_keys.total_font_size", 24);
			obs_data_set_default_int(settings, "live_keys.fade_ms", 300);
			obs_data_set_default_string(settings, "live_keys.fade_curve", "linear");
			obs_data_set_default_int(settings, "live_keys.color", 0xffeb6325);
			obs_data_set_default_int(settings, "live_keys.pressed_color", 0xff4444ef);
			obs_data_set_default_string(settings, "mouse_activity.left_label", "L");
			obs_data_set_default_string(settings, "mouse_activity.right_label", "R");
			obs_data_set_default_string(settings, "mouse_activity.middle_label", "M");
			obs_data_set_default_int(settings, "mouse_activity.trail_ms", 1500);
			obs_data_set_default_string(settings, "mouse_activity.heatmap_gradient", "spectrum");
			obs_data_set_default_int(settings, "mouse_activity.hex_size",
						 static_cast<int64_t>(default_heatmap_hex_radius));
			obs_data_set_default_int(settings, "mouse_activity.opacity", 100);
			obs_data_set_default_string(settings, "mouse_activity.map", "movement");
			obs_data_set_default_bool(settings, "mouse_activity.show_heatmap", true);
			obs_data_set_default_bool(settings, "mouse_activity.show_live_mouse", true);
			obs_data_set_default_bool(settings, "mouse_activity.show_coordinates", false);
			obs_data_set_default_bool(settings, "mouse_activity.show_distance", false);
			obs_data_set_default_string(settings, "mouse_activity.info_alignment", "left");
			obs_data_set_default_string(settings, "mouse_activity.distance_unit", "pixels");
			obs_data_set_default_int(settings, "mouse_activity.mouse_dpi", 800);
			obs_data_set_default_int(settings, "mouse_activity.color", 0xffeb6325);
			obs_data_set_default_int(settings, "mouse_activity.left_color", 0xffeb6325);
			obs_data_set_default_int(settings, "mouse_activity.right_color", 0xff4444ef);
			obs_data_set_default_int(settings, "mouse_activity.middle_color", 0xff15ccfa);
			obs_data_set_default_int(settings, "statistics.element_spacing", 10);
			obs_data_set_default_bool(settings, "statistics.show_key_rate", true);
			obs_data_set_default_bool(settings, "statistics.show_total_keys", true);
			obs_data_set_default_bool(settings, "statistics.show_click_rate", true);
			obs_data_set_default_bool(settings, "statistics.show_total_clicks", true);
			obs_data_set_default_bool(settings, "statistics.show_action_rate", true);
			obs_data_set_default_bool(settings, "statistics.show_total_actions", true);
			obs_data_set_default_int(settings, "input_intensity.window", 30);
			obs_data_set_default_int(settings, "input_intensity.element_spacing", 10);
			obs_data_set_default_int(settings, "input_intensity.color", 0xffeb6325);
			for (size_t index = 0; index < 8; ++index) {
				const std::string prefix = "input_intensity.row" + std::to_string(index) + ".";
				obs_data_set_default_bool(settings, (prefix + "enabled").c_str(), index == 0);
				obs_data_set_default_string(settings, (prefix + "metric").c_str(), "actions");
				obs_data_set_default_int(settings, (prefix + "key").c_str(), VC_SPACE);
				obs_data_set_default_int(settings, (prefix + "button").c_str(), MOUSE_BUTTON1);
				obs_data_set_default_string(settings, (prefix + "title").c_str(), "");
				obs_data_set_default_string(settings, (prefix + "key_scope").c_str(), "all");
				obs_data_set_default_string(settings, (prefix + "key_list").c_str(), "");
			}
		}
		if constexpr (std::is_same_v<T, live_keys_source>) {
			obs_data_set_default_string(settings, "activity.title", "Live Keys");
			obs_data_set_default_bool(settings, "activity.show_title", false);
			obs_data_set_default_int(settings, "live_keys.maximum", 8);
			obs_data_set_default_int(settings, "live_keys.top_n", 8);
			obs_data_set_default_int(settings, "live_keys.row_height", 96);
			obs_data_set_default_bool(settings, "live_keys.row_layout", true);
			obs_data_set_default_string(settings, "live_keys.top_n_alignment", "left");
			obs_data_set_default_int(settings, "live_keys.element_spacing", 10);
			obs_data_set_default_bool(settings, "live_keys.show_most_used", false);
			obs_data_set_default_bool(settings, "live_keys.show_live_title", true);
			obs_data_set_default_string(settings, "live_keys.live_title", "Live Keys");
			obs_data_set_default_int(settings, "live_keys.live_title_font_size", 28);
			obs_data_set_default_bool(settings, "live_keys.show_most_used_title", true);
			obs_data_set_default_string(settings, "live_keys.most_used_title", "Most Used Keys");
			obs_data_set_default_int(settings, "live_keys.most_used_title_font_size", 28);
			obs_data_set_default_int(settings, "live_keys.key_font_size", 36);
			obs_data_set_default_int(settings, "live_keys.special_key_font_size", 28);
			obs_data_set_default_int(settings, "live_keys.total_font_size", 24);
			obs_data_set_default_int(settings, "live_keys.fade_ms", 300);
			obs_data_set_default_string(settings, "live_keys.fade_curve", "linear");
			obs_data_set_default_int(settings, "live_keys.color", 0xffeb6325);
			obs_data_set_default_int(settings, "live_keys.pressed_color", 0xff4444ef);
		} else if constexpr (std::is_same_v<T, mouse_activity_source>) {
			obs_data_set_default_string(settings, "activity.title", "Mouse Activity");
			obs_data_set_default_string(settings, "mouse_activity.left_label", "L");
			obs_data_set_default_string(settings, "mouse_activity.right_label", "R");
			obs_data_set_default_string(settings, "mouse_activity.middle_label", "M");
			obs_data_set_default_bool(settings, "mouse_activity.show_coordinates", false);
			obs_data_set_default_bool(settings, "mouse_activity.show_heatmap", true);
			obs_data_set_default_bool(settings, "mouse_activity.show_live_mouse", true);
			obs_data_set_default_bool(settings, "mouse_activity.show_distance", false);
			obs_data_set_default_string(settings, "mouse_activity.info_alignment", "left");
			obs_data_set_default_string(settings, "mouse_activity.distance_unit", "pixels");
			obs_data_set_default_int(settings, "mouse_activity.mouse_dpi", 800);
			obs_data_set_default_bool(settings, "mouse_activity.show_border", false);
			obs_data_set_default_bool(settings, "mouse_activity.show_center_mark", false);
			obs_data_set_default_int(settings, "mouse_activity.trail_ms", 1500);
			obs_data_set_default_string(settings, "mouse_activity.heatmap_gradient", "spectrum");
			obs_data_set_default_int(settings, "mouse_activity.hex_size",
						 static_cast<int64_t>(default_heatmap_hex_radius));
			obs_data_set_default_int(settings, "mouse_activity.opacity", 100);
			obs_data_set_default_string(
				settings, "mouse_activity.export_directory",
				QStandardPaths::writableLocation(QStandardPaths::PicturesLocation).toUtf8().constData());
			obs_data_set_default_string(settings, "mouse_activity.export_format", "png");
			obs_data_set_default_int(settings, "mouse_activity.color", 0xffeb6325);
			obs_data_set_default_int(settings, "mouse_activity.left_color", 0xffeb6325);
			obs_data_set_default_int(settings, "mouse_activity.right_color", 0xff4444ef);
			obs_data_set_default_int(settings, "mouse_activity.middle_color", 0xff15ccfa);
			obs_data_set_default_string(settings, "mouse_activity.map", "movement");
		} else if constexpr (std::is_same_v<T, statistics_source>) {
			obs_data_set_default_string(settings, "activity.title", "Input Statistics");
			obs_data_set_default_bool(settings, "statistics.show_key_rate", true);
			obs_data_set_default_bool(settings, "statistics.show_total_keys", true);
			obs_data_set_default_bool(settings, "statistics.show_click_rate", true);
			obs_data_set_default_bool(settings, "statistics.show_total_clicks", true);
			obs_data_set_default_bool(settings, "statistics.show_action_rate", true);
			obs_data_set_default_bool(settings, "statistics.show_total_actions", true);
			obs_data_set_default_int(settings, "statistics.element_spacing", 10);
			obs_data_set_default_bool(settings, "statistics.show_lap_keys", false);
			obs_data_set_default_bool(settings, "statistics.show_lap_clicks", false);
			obs_data_set_default_bool(settings, "statistics.show_lap_actions", false);
		} else if constexpr (std::is_same_v<T, input_intensity_source>) {
			obs_data_set_default_string(settings, "activity.title", "Input Intensity");
			obs_data_set_default_int(settings, "input_intensity.window", 30);
			obs_data_set_default_int(settings, "input_intensity.element_spacing", 10);
			obs_data_set_default_int(settings, "input_intensity.color", 0xffeb6325);
			for (size_t index = 0; index < 8; ++index) {
				const std::string prefix = "input_intensity.row" + std::to_string(index) + ".";
				obs_data_set_default_bool(settings, (prefix + "enabled").c_str(), index == 0);
				obs_data_set_default_string(settings, (prefix + "metric").c_str(), "actions");
				obs_data_set_default_int(settings, (prefix + "key").c_str(), VC_SPACE);
				obs_data_set_default_int(settings, (prefix + "button").c_str(), MOUSE_BUTTON1);
				obs_data_set_default_string(settings, (prefix + "title").c_str(), "");
				obs_data_set_default_string(settings, (prefix + "key_scope").c_str(), "all");
				obs_data_set_default_string(settings, (prefix + "key_list").c_str(), "");
			}
		}
	};
	obs_register_source(&info);
}
obs_properties_t *keys_properties_impl(void *, bool include_common)
{
	auto *p = obs_properties_create();
	if (include_common)
		add_common_properties(p);
	obs_properties_add_int(p, "live_keys.maximum", obs_module_text("LiveKeys.Maximum"), 1, 64, 1);
	obs_properties_add_int(p, "live_keys.row_height", obs_module_text("LiveKeys.RowHeight"), 24, 2160, 1);
	obs_properties_add_bool(p, "live_keys.show_live_title", obs_module_text("LiveKeys.ShowLiveTitle"));
	obs_properties_add_text(p, "live_keys.live_title", obs_module_text("LiveKeys.LiveTitle"), OBS_TEXT_DEFAULT);
	obs_properties_add_int(p, "live_keys.live_title_font_size", obs_module_text("LiveKeys.TitleFontSize"), 8, 256,
			       1);
	obs_properties_add_int(p, "live_keys.top_n", obs_module_text("LiveKeys.TopN"), 1, 64, 1);
	auto *top_n_alignment = obs_properties_add_list(p, "live_keys.top_n_alignment",
							obs_module_text("LiveKeys.TopNAlignment"), OBS_COMBO_TYPE_LIST,
							OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(top_n_alignment, obs_module_text("LiveKeys.TopNAlignment.Left"), "left");
	obs_property_list_add_string(top_n_alignment, obs_module_text("LiveKeys.TopNAlignment.Right"), "right");
	obs_properties_add_int_slider(p, "live_keys.element_spacing", obs_module_text("Activity.ElementSpacing"), 0,
				      200, 1);
	obs_properties_add_bool(p, "live_keys.show_most_used", obs_module_text("LiveKeys.ShowMostUsed"));
	obs_properties_add_bool(p, "live_keys.show_most_used_title", obs_module_text("LiveKeys.ShowMostUsedTitle"));
	obs_properties_add_text(p, "live_keys.most_used_title", obs_module_text("LiveKeys.MostUsedTitle"),
				OBS_TEXT_DEFAULT);
	obs_properties_add_int(p, "live_keys.most_used_title_font_size", obs_module_text("LiveKeys.TitleFontSize"), 8,
			       256, 1);
	obs_properties_add_int(p, "live_keys.key_font_size", obs_module_text("LiveKeys.KeyFontSize"), 8, 256, 1);
	obs_properties_add_int(p, "live_keys.special_key_font_size", obs_module_text("LiveKeys.SpecialKeyFontSize"), 8,
			       256, 1);
	obs_properties_add_int(p, "live_keys.total_font_size", obs_module_text("LiveKeys.TotalFontSize"), 8, 256, 1);
	obs_properties_add_int_slider(p, "live_keys.fade_ms", obs_module_text("LiveKeys.FadeDuration"), 0, 5000, 10);
	auto *fade_curve = obs_properties_add_list(p, "live_keys.fade_curve", obs_module_text("LiveKeys.FadeCurve"),
						   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(fade_curve, obs_module_text("LiveKeys.FadeCurve.Linear"), "linear");
	obs_property_list_add_string(fade_curve, obs_module_text("LiveKeys.FadeCurve.EaseIn"), "ease_in");
	obs_property_list_add_string(fade_curve, obs_module_text("LiveKeys.FadeCurve.EaseOut"), "ease_out");
	obs_property_list_add_string(fade_curve, obs_module_text("LiveKeys.FadeCurve.EaseInOut"), "ease_in_out");
	obs_properties_add_color_alpha(p, "live_keys.color", obs_module_text("LiveKeys.ThemeColor"));
	obs_properties_add_color_alpha(p, "live_keys.pressed_color", obs_module_text("LiveKeys.PressedColor"));
	return p;
}
obs_properties_t *keys_properties(void *data)
{
	return keys_properties_impl(data, true);
}

obs_properties_t *mouse_properties_impl(void *data, bool include_common)
{
	auto *p = obs_properties_create();
	if (include_common)
		add_common_properties(p, false);
	add_mode_title_properties(p);
	obs_properties_add_text(p, "mouse_activity.left_label", obs_module_text("MouseActivity.LeftLabel"),
				OBS_TEXT_DEFAULT);
	obs_properties_add_text(p, "mouse_activity.right_label", obs_module_text("MouseActivity.RightLabel"),
				OBS_TEXT_DEFAULT);
	obs_properties_add_text(p, "mouse_activity.middle_label", obs_module_text("MouseActivity.MiddleLabel"),
				OBS_TEXT_DEFAULT);
	obs_properties_add_color_alpha(p, "mouse_activity.left_color", obs_module_text("MouseActivity.LeftColor"));
	obs_properties_add_color_alpha(p, "mouse_activity.right_color", obs_module_text("MouseActivity.RightColor"));
	obs_properties_add_color_alpha(p, "mouse_activity.middle_color", obs_module_text("MouseActivity.MiddleColor"));
	obs_properties_add_bool(p, "mouse_activity.show_heatmap", obs_module_text("MouseActivity.ShowHeatmap"));
	obs_properties_add_bool(p, "mouse_activity.show_live_mouse", obs_module_text("MouseActivity.ShowLiveMouse"));
	obs_properties_add_bool(p, "mouse_activity.show_coordinates", obs_module_text("MouseActivity.ShowCoordinates"));
	obs_properties_add_bool(p, "mouse_activity.show_distance", obs_module_text("MouseActivity.ShowDistance"));
	auto *information_alignment = obs_properties_add_list(p, "mouse_activity.info_alignment",
							      obs_module_text("MouseActivity.InformationAlignment"),
							      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(information_alignment, obs_module_text("MouseActivity.InformationAlignment.Left"),
				     "left");
	obs_property_list_add_string(information_alignment, obs_module_text("MouseActivity.InformationAlignment.Right"),
				     "right");
	auto *distance_unit = obs_properties_add_list(p, "mouse_activity.distance_unit",
						      obs_module_text("MouseActivity.DistanceUnit"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(distance_unit, obs_module_text("MouseActivity.DistanceUnit.Pixels"), "pixels");
	obs_property_list_add_string(distance_unit, obs_module_text("MouseActivity.DistanceUnit.Metric"), "metric");
	obs_property_list_add_string(distance_unit, obs_module_text("MouseActivity.DistanceUnit.Imperial"), "imperial");
	obs_properties_add_int(p, "mouse_activity.mouse_dpi", obs_module_text("MouseActivity.MouseDPI"), 1, 100000, 1);
	obs_properties_add_bool(p, "mouse_activity.show_border", obs_module_text("MouseActivity.ShowBorder"));
	obs_properties_add_bool(p, "mouse_activity.show_center_mark", obs_module_text("MouseActivity.ShowCenterMark"));
	obs_properties_add_int_slider(p, "mouse_activity.trail_ms", obs_module_text("MouseActivity.TrailDuration"), 100,
				      10000, 50);
	auto *gradient = obs_properties_add_list(p, "mouse_activity.heatmap_gradient",
						 obs_module_text("MouseActivity.HeatmapGradient"), OBS_COMBO_TYPE_LIST,
						 OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(gradient, obs_module_text("MouseActivity.HeatmapGradient.Spectrum"), "spectrum");
	obs_property_list_add_string(gradient, obs_module_text("MouseActivity.HeatmapGradient.Lime"), "lime");
	obs_property_list_add_string(gradient, obs_module_text("MouseActivity.HeatmapGradient.Ocean"), "ocean");
	obs_properties_add_int_slider(p, "mouse_activity.hex_size", obs_module_text("MouseActivity.HexSize"), 2, 100,
				      1);
	obs_properties_add_int_slider(p, "mouse_activity.opacity", obs_module_text("MouseActivity.HeatmapOpacity"), 0,
				      100, 1);
	auto *map = obs_properties_add_list(p, "mouse_activity.map", obs_module_text("MouseActivity.Map"),
					    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(map, obs_module_text("MouseActivity.Map.Movement"), "movement");
	obs_property_list_add_string(map, obs_module_text("MouseActivity.Map.Clicks"), "clicks");
	obs_properties_add_color_alpha(p, "mouse_activity.color", obs_module_text("Activity.ActiveColor"));
	obs_properties_add_path(p, "mouse_activity.export_directory", obs_module_text("MouseActivity.ExportDirectory"),
				OBS_PATH_DIRECTORY, nullptr, nullptr);
	auto *export_format = obs_properties_add_list(p, "mouse_activity.export_format",
						      obs_module_text("MouseActivity.ExportFormat"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(export_format, obs_module_text("MouseActivity.ExportFormat.PNG"), "png");
	obs_property_list_add_string(export_format, obs_module_text("MouseActivity.ExportFormat.SVG"), "svg");
	auto *displays = obs_properties_add_list(p, "mouse_activity.display", obs_module_text("MouseActivity.Display"),
						 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	unsigned char count{};
	std::unique_ptr<screen_data, decltype(&free)> screens(hook_create_screen_info(&count), &free);
	for (int i = 0; screens && i < count; ++i) {
		const auto &s = screens.get()[i];
		const QByteArray label = QString("Display %1 (%2x%3 at %4,%5)")
						 .arg(i + 1)
						 .arg(s.width)
						 .arg(s.height)
						 .arg(s.x)
						 .arg(s.y)
						 .toUtf8();
		obs_property_list_add_int(displays, label.constData(), i);
	}
	obs_properties_add_button2(
		p, "mouse_activity.clear", obs_module_text("MouseActivity.ClearHeatmap"),
		[](obs_properties_t *, obs_property_t *, void *d) {
			static_cast<mouse_activity_source *>(d)->clear();
			return true;
		},
		data);
	return p;
}
obs_properties_t *mouse_properties(void *data)
{
	return mouse_properties_impl(data, true);
}

obs_properties_t *statistics_properties_impl(void *, bool include_common)
{
	auto *p = obs_properties_create();
	if (include_common)
		add_common_properties(p);
	add_mode_title_properties(p);
	obs_properties_add_bool(p, "statistics.show_key_rate", obs_module_text("Statistics.ShowKeyRate"));
	obs_properties_add_bool(p, "statistics.show_total_keys", obs_module_text("Statistics.ShowTotalKeys"));
	obs_properties_add_bool(p, "statistics.show_click_rate", obs_module_text("Statistics.ShowClickRate"));
	obs_properties_add_bool(p, "statistics.show_total_clicks", obs_module_text("Statistics.ShowTotalClicks"));
	obs_properties_add_bool(p, "statistics.show_action_rate", obs_module_text("Statistics.ShowActionRate"));
	obs_properties_add_bool(p, "statistics.show_total_actions", obs_module_text("Statistics.ShowTotalActions"));
	obs_properties_add_int_slider(p, "statistics.element_spacing", obs_module_text("Activity.ElementSpacing"), 0,
				      200, 1);
	obs_properties_add_bool(p, "statistics.show_lap_keys", obs_module_text("Statistics.ShowLapKeys"));
	obs_properties_add_bool(p, "statistics.show_lap_clicks", obs_module_text("Statistics.ShowLapClicks"));
	obs_properties_add_bool(p, "statistics.show_lap_actions", obs_module_text("Statistics.ShowLapActions"));
	return p;
}
obs_properties_t *statistics_properties(void *data)
{
	return statistics_properties_impl(data, true);
}

void add_intensity_key_list(obs_property_t *list)
{
	for (uint16_t code = VC_A; code <= VC_Z; ++code) {
		input_data::trace_event event{};
		event.code = code;
		const QByteArray label = key_name(event).toUtf8();
		obs_property_list_add_int(list, label.constData(), code);
	}
	for (uint16_t code = VC_0; code <= VC_9; ++code) {
		input_data::trace_event event{};
		event.code = code;
		const QByteArray label = key_name(event).toUtf8();
		obs_property_list_add_int(list, label.constData(), code);
	}
	const uint16_t common_codes[] = {VC_SPACE,     VC_ENTER,     VC_ESCAPE,       VC_TAB,           VC_BACKSPACE,
					 VC_SHIFT_L,   VC_CONTROL_L, VC_ALT_L,        VC_META_L,        VC_CAPS_LOCK,
					 VC_UP,        VC_DOWN,      VC_LEFT,         VC_RIGHT,         VC_HOME,
					 VC_END,       VC_PAGE_UP,   VC_PAGE_DOWN,    VC_INSERT,        VC_DELETE,
					 VC_MINUS,     VC_EQUALS,    VC_OPEN_BRACKET, VC_CLOSE_BRACKET, VC_BACK_SLASH,
					 VC_SEMICOLON, VC_QUOTE,     VC_COMMA,        VC_PERIOD,        VC_SLASH,
					 VC_BACK_QUOTE};
	for (const uint16_t code : common_codes) {
		input_data::trace_event event{};
		event.code = code;
		const QByteArray label = key_name(event).toUtf8();
		obs_property_list_add_int(list, label.constData(), code);
	}
	for (uint16_t code = VC_F1; code <= VC_F12; ++code) {
		input_data::trace_event event{};
		event.code = code;
		const QByteArray label = key_name(event).toUtf8();
		obs_property_list_add_int(list, label.constData(), code);
	}
	for (uint16_t code = VC_F13; code <= VC_F24; ++code) {
		input_data::trace_event event{};
		event.code = code;
		const QByteArray label = key_name(event).toUtf8();
		obs_property_list_add_int(list, label.constData(), code);
	}
	for (uint16_t code = VC_KP_0; code <= VC_KP_9; ++code) {
		input_data::trace_event event{};
		event.code = code;
		const QByteArray label = key_name(event).toUtf8();
		obs_property_list_add_int(list, label.constData(), code);
	}
	const uint16_t keypad_codes[] = {VC_KP_DIVIDE, VC_KP_MULTIPLY, VC_KP_SUBTRACT,
					 VC_KP_ADD,    VC_KP_DECIMAL,  VC_KP_ENTER};
	for (const uint16_t code : keypad_codes) {
		input_data::trace_event event{};
		event.code = code;
		const QByteArray label = key_name(event).toUtf8();
		obs_property_list_add_int(list, label.constData(), code);
	}
}

obs_properties_t *intensity_properties_impl(void *, bool include_common)
{
	auto *p = obs_properties_create();
	if (include_common)
		add_common_properties(p);
	add_mode_title_properties(p);
	obs_properties_add_int_slider(p, "input_intensity.window", obs_module_text("InputIntensity.Window"), 1, 60, 1);
	obs_properties_add_int_slider(p, "input_intensity.element_spacing", obs_module_text("Activity.ElementSpacing"),
				      0, 200, 1);
	obs_properties_add_color_alpha(p, "input_intensity.color", obs_module_text("InputIntensity.Color"));
	for (size_t index = 0; index < 8; ++index) {
		const std::string prefix = "input_intensity.row" + std::to_string(index) + ".";
		const QByteArray row_label =
			QString("%1 %2").arg(obs_module_text("InputIntensity.Row")).arg(index + 1).toUtf8();
		obs_properties_add_bool(p, (prefix + "enabled").c_str(), row_label.constData());
		obs_properties_add_text(p, (prefix + "title").c_str(), obs_module_text("InputIntensity.Title"),
					OBS_TEXT_DEFAULT);
		auto *metric = obs_properties_add_list(p, (prefix + "metric").c_str(),
						       obs_module_text("InputIntensity.Metric"), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_STRING);
		obs_property_list_add_string(metric, obs_module_text("InputIntensity.Metric.Actions"), "actions");
		obs_property_list_add_string(metric, obs_module_text("InputIntensity.Metric.Keyboard"), "keyboard");
		obs_property_list_add_string(metric, obs_module_text("InputIntensity.Metric.Mouse"), "mouse");
		obs_property_list_add_string(metric, obs_module_text("InputIntensity.Metric.Key"), "key");
		obs_property_list_add_string(metric, obs_module_text("InputIntensity.Metric.MouseButton"), "button");
		obs_property_list_add_string(metric, obs_module_text("InputIntensity.Metric.Velocity"), "velocity");
		auto *key_scope = obs_properties_add_list(p, (prefix + "key_scope").c_str(),
							  obs_module_text("InputIntensity.KeyScope"),
							  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_list_add_string(key_scope, obs_module_text("InputIntensity.KeyScope.All"), "all");
		obs_property_list_add_string(key_scope, obs_module_text("InputIntensity.KeyScope.Letters"), "letters");
		obs_property_list_add_string(key_scope, obs_module_text("InputIntensity.KeyScope.Numbers"), "numbers");
		obs_property_list_add_string(key_scope, obs_module_text("InputIntensity.KeyScope.List"), "list");
		obs_properties_add_text(p, (prefix + "key_list").c_str(), obs_module_text("InputIntensity.KeyList"),
					OBS_TEXT_DEFAULT);
		auto *key = obs_properties_add_list(p, (prefix + "key").c_str(), obs_module_text("InputIntensity.Key"),
						    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		add_intensity_key_list(key);
		auto *button = obs_properties_add_list(p, (prefix + "button").c_str(),
						       obs_module_text("InputIntensity.Button"), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_INT);
		for (uint16_t code = MOUSE_BUTTON1; code <= MOUSE_BUTTON5; ++code) {
			const QByteArray label = QString("%1 %2")
							 .arg(obs_module_text("InputIntensity.Metric.MouseButton"))
							 .arg(code)
							 .toUtf8();
			obs_property_list_add_int(button, label.constData(), code);
		}
	}
	return p;
}
obs_properties_t *intensity_properties(void *data)
{
	return intensity_properties_impl(data, true);
}

bool unified_mode_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const std::string mode = obs_data_get_string(settings, "input_activity.mode");
	obs_property_set_visible(obs_properties_get(props, "input_activity.live_keys"), mode == "live_keys");
	obs_property_set_visible(obs_properties_get(props, "input_activity.mouse_activity"), mode == "mouse_activity");
	obs_property_set_visible(obs_properties_get(props, "input_activity.input_intensity"),
				 mode == "input_intensity");
	obs_property_set_visible(obs_properties_get(props, "input_activity.input_statistics"),
				 mode == "input_statistics");
	return true;
}

obs_properties_t *unified_properties(void *data)
{
	auto *p = obs_properties_create();
	auto *mode = obs_properties_add_list(p, "input_activity.mode", obs_module_text("InputActivity.Mode"),
					     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(mode, obs_module_text("LiveKeys"), "live_keys");
	obs_property_list_add_string(mode, obs_module_text("MouseActivity"), "mouse_activity");
	obs_property_list_add_string(mode, obs_module_text("InputIntensity"), "input_intensity");
	obs_property_list_add_string(mode, obs_module_text("InputStatistics"), "input_statistics");
	obs_property_set_modified_callback(mode, unified_mode_changed);
	add_common_properties(p);
	auto *unified = static_cast<unified_source *>(data);
	auto *live_group = obs_properties_add_group(p, "input_activity.live_keys", obs_module_text("LiveKeys"),
						    OBS_GROUP_NORMAL, keys_properties_impl(data, false));
	auto *mouse_group = obs_properties_add_group(
		p, "input_activity.mouse_activity", obs_module_text("MouseActivity"), OBS_GROUP_NORMAL,
		mouse_properties_impl(unified ? unified->mouse() : nullptr, false));
	auto *intensity_group = obs_properties_add_group(p, "input_activity.input_intensity",
							 obs_module_text("InputIntensity"), OBS_GROUP_NORMAL,
							 intensity_properties_impl(data, false));
	auto *statistics_group = obs_properties_add_group(p, "input_activity.input_statistics",
							  obs_module_text("InputStatistics"), OBS_GROUP_NORMAL,
							  statistics_properties_impl(data, false));
	obs_property_set_visible(live_group, true);
	obs_property_set_visible(mouse_group, false);
	obs_property_set_visible(intensity_group, false);
	obs_property_set_visible(statistics_group, false);
	return p;
}
} // namespace

void register_activity_sources()
{
	register_source<live_keys_source>("input-activity-live-keys", keys_properties);
	register_source<input_intensity_source>("input-activity-input-intensity", intensity_properties);
	register_source<mouse_activity_source>("input-activity-mouse-activity", mouse_properties);
	register_source<statistics_source>("input-activity-statistics", statistics_properties);
	register_source<unified_source>("input-activity", unified_properties);
}
} // namespace sources
