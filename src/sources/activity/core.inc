#include "../activity_sources.hpp"

#include "../../hook/uiohook_helper.hpp"
#include "../../input/input_data.hpp"
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
constexpr uint64_t statistics_highlight_duration_ns = 300ULL * 1000 * 1000;
constexpr uint64_t max_heatmap_gap_ns = 250ULL * 1000 * 1000;
constexpr qreal default_heatmap_hex_radius = 10.0;
constexpr int default_title_font_size = 22;
constexpr int default_subtitle_font_size = 18;
constexpr int default_text_font_size = 30;

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

void migrate_legacy_typography(obs_data_t *settings, const char *migration_key, const char *legacy_title,
			       const char *legacy_subtitle, const char *legacy_text)
{
	if (obs_data_get_bool(settings, migration_key))
		return;
	const auto migrate = [&](const char *shared_key, const char *legacy_key) {
		if (legacy_key && !obs_data_has_user_value(settings, shared_key) &&
		    obs_data_has_user_value(settings, legacy_key))
			obs_data_set_int(settings, shared_key, obs_data_get_int(settings, legacy_key));
	};
	migrate("activity.section_title_size", legacy_title);
	migrate("activity.input_title_size", legacy_subtitle);
	migrate("activity.input_value_size", legacy_text);
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
		text_size = std::max(8, static_cast<int>(obs_data_get_int(settings, "activity.input_value_size")));
		title_size = std::max(8, static_cast<int>(obs_data_get_int(settings, "activity.section_title_size")));
		subtitle_size = std::max(8, static_cast<int>(obs_data_get_int(settings, "activity.input_title_size")));
		title_right_aligned = std::string(obs_data_get_string(settings, "activity.alignment")) == "right";
		text_color = QColor{255, 255, 255};
		background_color =
			obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "activity.background_color")));
		text_shadow = true;
		text_shadow_color = QColor{0, 0, 0};
		text_shadow_offset = 2;
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
		target_rectangle_enabled = obs_data_get_bool(settings, "activity.target.display_rectangle");
		target_rectangle = QRect(
			static_cast<int>(obs_data_get_int(settings, "activity.target.rectangle.left")),
			static_cast<int>(obs_data_get_int(settings, "activity.target.rectangle.top")),
			static_cast<int>(obs_data_get_int(settings, "activity.target.rectangle.right")) -
				static_cast<int>(obs_data_get_int(settings, "activity.target.rectangle.left")) + 1,
			static_cast<int>(obs_data_get_int(settings, "activity.target.rectangle.bottom")) -
				static_cast<int>(obs_data_get_int(settings, "activity.target.rectangle.top")) + 1);
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
		image = QImage(width, height, QImage::Format_RGBA8888_Premultiplied);
		image.fill(background_color);
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing);
		if (show_title && !title.isEmpty()) {
			painter.setFont(font(title_size));
			const int title_height = title_content_offset() - padding / 2;
			const int title_alignment = title_right_aligned ? Qt::AlignRight : Qt::AlignLeft;
			draw_text(painter, QRect(padding, padding, std::max(1, width - padding * 2), title_height),
				  title_alignment | Qt::AlignVCenter, title, text_color);
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
		result.setPixelSize(pixel_size > 0 ? pixel_size : text_size);
		return result;
	}
	int title_content_offset() const
	{
		return show_title && !title.isEmpty() ? QFontMetrics(font(title_size)).lineSpacing() + padding : 0;
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
	void draw_text(QPainter &painter, const QPoint &baseline, const QString &text, const QColor &color) const
	{
		if (text_shadow) {
			QColor shadow = text_shadow_color;
			shadow.setAlpha(shadow.alpha() * color.alpha() / 255);
			painter.setPen(shadow);
			painter.drawText(baseline + QPoint(text_shadow_offset, text_shadow_offset), text);
		}
		painter.setPen(color);
		painter.drawText(baseline, text);
	}
	obs_source_t *source{};
	int width = 480, height = 180, padding = 12, title_size = default_title_font_size,
	    subtitle_size = default_subtitle_font_size, text_size = default_text_font_size;
	bool title_right_aligned{};
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
			if ((mouse_motion ? event.pointer_display_id : event.focused_display_id) != target_display)
				return false;
			return !target_rectangle_enabled || !mouse_motion ||
			       target_rectangle.contains(event.x, event.y);
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
	bool target_rectangle_enabled{};
	QRect target_rectangle;
	uint64_t target_window{};
	std::string target_application;
	std::string target_window_application;
	QImage image;
	gs_texture_t *texture{};
	int texture_width{}, texture_height{};
};

std::mutex activity_source::activity_sources_mutex;
std::unordered_set<activity_source *> activity_source::activity_sources;
