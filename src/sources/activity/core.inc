#include "../activity_sources.hpp"

#include "../../hook/uiohook_helper.hpp"
#include "../../input/input_broker.hpp"
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
	activity_source(obs_source_t *source, obs_data_t *settings, bool = true, bool initial_update = true)
		: source(source)
	{
		if (initial_update)
			obs_source_update(source, settings);
	}
	virtual ~activity_source()
	{
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
		const uint32_t target_display =
			static_cast<uint32_t>(obs_data_get_int(settings, "activity.target.display"));
		const bool target_rectangle_enabled = obs_data_get_bool(settings, "activity.target.display_rectangle");
		const int rectangle_left =
			static_cast<int>(obs_data_get_int(settings, "activity.target.rectangle.left"));
		const int rectangle_top = static_cast<int>(obs_data_get_int(settings, "activity.target.rectangle.top"));
		const int rectangle_right =
			static_cast<int>(obs_data_get_int(settings, "activity.target.rectangle.right"));
		const int rectangle_bottom =
			static_cast<int>(obs_data_get_int(settings, "activity.target.rectangle.bottom"));
		const std::string target_application = obs_data_get_string(settings, "activity.target.application");
		const std::string window = obs_data_get_string(settings, "activity.target.window");
		const size_t delimiter = window.rfind('#');
		std::string target_window_application;
		uint64_t target_window{};
		if (delimiter != std::string::npos) {
			target_window_application = window.substr(0, delimiter);
			try {
				target_window = std::stoull(window.substr(delimiter + 1));
			} catch (...) {
				target_window = 0;
			}
		}
		input_broker::target new_broker_target;
		new_broker_target.type = selected_target_type == "display" ? input_broker::target_type::display
					 : selected_target_type == "application"
						 ? input_broker::target_type::application
					 : selected_target_type == "window" ? input_broker::target_type::window
									    : input_broker::target_type::all;
		new_broker_target.display = target_display;
		new_broker_target.rectangle_enabled = target_rectangle_enabled;
		new_broker_target.rectangle_left = rectangle_left;
		new_broker_target.rectangle_top = rectangle_top;
		new_broker_target.rectangle_right = rectangle_right;
		new_broker_target.rectangle_bottom = rectangle_bottom;
		new_broker_target.application_id = new_broker_target.type == input_broker::target_type::window
							   ? target_window_application
							   : target_application;
		new_broker_target.window_id = target_window;
		if (target_configured && !(new_broker_target == broker_target))
			discard_backlog = true;
		broker_target = std::move(new_broker_target);
		target_configured = true;
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
			draw_text(painter, QRect(padding, padding, std::max(1, width - padding * 2), title_height),
				  (title_right_aligned ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, title,
				  text_color);
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
		input_broker::consume(broker_target, cursor, discard_backlog, events, keyboard, mouse);
		for (const auto &event : events)
			on_event(event);
		on_snapshot(keyboard, mouse);
	}
	virtual void on_event(const input_data::trace_event &) {}
	virtual void on_snapshot(const input_data::button_map<uint16_t> &, const input_data::button_map<uint16_t> &) {}
	virtual void reset_activity() {}
	virtual void lap_activity() {}
	const input_broker::target &input_target() const { return broker_target; }
	virtual void render(QPainter &) = 0;
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
	input_broker::target broker_target;
	bool target_configured{};
	bool discard_backlog{};

private:
	QImage image;
	gs_texture_t *texture{};
	int texture_width{}, texture_height{};
};
