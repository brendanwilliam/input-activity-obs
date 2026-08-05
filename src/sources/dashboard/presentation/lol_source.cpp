#include "hook/uiohook_helper.hpp"
#include "input/input_broker.hpp"
#include "sources/dashboard/capture/lol_capture_switcher.hpp"
#include "sources/dashboard/detection/lol_game_config_watcher.hpp"
#include "sources/dashboard/detection/lol_game_start_watcher.hpp"
#include "sources/dashboard/presentation/lol_source.hpp"
#include "sources/dashboard/rendering/lol_camera_visibility.hpp"
#include "sources/dashboard/rendering/lol_layout.hpp"
#include "sources/dashboard/rendering/lol_visuals.hpp"
#include "sources/hud_layout/lol_layout.hpp"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QStringList>
#include <algorithm>
#include <array>
#include <memory>
#include <obs-module.h>
#include <obs-hotkey.h>
#include <util/bmem.h>
extern "C" {
#include <graphics/graphics.h>
}

namespace sources {
namespace {
constexpr const char *source_id = "input-activity-lol-performance-dashboard";
constexpr const char *path_key = "lol_dashboard.game_cfg";

QColor obs_color(uint32_t value)
{
	return {int(value & 0xff), int((value >> 8) & 0xff), int((value >> 16) & 0xff), int((value >> 24) & 0xff)};
}

QRect qrect(const lol_dashboard_rect &rect)
{
	return {rect.x(), rect.y(), rect.width(), rect.height()};
}

class dashboard_source {
public:
	dashboard_source(obs_source_t *source, obs_data_t *settings) : source_(source)
	{
		reset_hotkey_ = obs_hotkey_register_source(
			source_, "reset_lol_performance_dashboard",
			obs_module_text("LoLPerformanceDashboard.ResetStatistics"),
			[](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
				if (pressed)
					static_cast<dashboard_source *>(data)->reset_statistics();
			},
			this);
		update(settings);
	}
	~dashboard_source()
	{
		obs_hotkey_unregister(reset_hotkey_);
		if (texture_ || camera_texture_) {
			obs_enter_graphics();
			gs_texture_destroy(texture_);
			gs_texrender_destroy(camera_texture_);
			obs_leave_graphics();
		}
	}

	void update(obs_data_t *settings)
	{
		path_ = QString::fromUtf8(obs_data_get_string(settings, path_key));
		game_config_watcher_.set_path(path_);
		advanced_positioning_ = obs_data_get_bool(settings, "lol_dashboard.advanced_positioning");
		always_visible_ = obs_data_get_bool(settings, "lol_dashboard.always_visible");
		auto_reset_at_game_start_ = obs_data_get_bool(settings, "lol_dashboard.reset_at_game_start");
		game_capture_source_ = obs_data_get_string(settings, league_capture_switcher::game_source_key);
		client_capture_source_ = obs_data_get_string(settings, league_capture_switcher::client_source_key);
		show_camera_ = obs_data_get_bool(settings, "lol_dashboard.show_camera");
		const std::string selected_camera_source = obs_data_get_string(settings, "lol_dashboard.camera_source");
		const bool camera_link_changed = camera_source_initialized_ &&
						 camera_source_uuid_ != selected_camera_source;
		camera_source_uuid_ = selected_camera_source;
		camera_source_initialized_ = true;
		camera_width_percent_ = int(obs_data_get_int(settings, "lol_dashboard.camera_width_percent"));
		camera_height_percent_ = int(obs_data_get_int(settings, "lol_dashboard.camera_height_percent"));
		camera_scale_percent_ = camera_link_changed
						? 100
						: int(obs_data_get_int(settings, "lol_dashboard.camera_scale_percent"));
		camera_translate_x_percent_ =
			camera_link_changed
				? 0
				: int(obs_data_get_int(settings, "lol_dashboard.camera_translate_x_percent"));
		camera_translate_y_percent_ =
			camera_link_changed
				? 0
				: int(obs_data_get_int(settings, "lol_dashboard.camera_translate_y_percent"));
		if (camera_link_changed) {
			obs_data_set_int(settings, "lol_dashboard.camera_scale_percent", camera_scale_percent_);
			obs_data_set_int(settings, "lol_dashboard.camera_translate_x_percent",
					 camera_translate_x_percent_);
			obs_data_set_int(settings, "lol_dashboard.camera_translate_y_percent",
					 camera_translate_y_percent_);
		}
		camera_background_color_ =
			obs_color(uint32_t(obs_data_get_int(settings, "lol_dashboard.camera_background_color")));
		show_minimap_cover_ = obs_data_get_bool(settings, "lol_dashboard.show_minimap_cover");
		use_custom_minimap_cover_ = obs_data_get_bool(settings, "lol_dashboard.use_custom_minimap_cover");
		const QString custom_cover_path =
			QString::fromUtf8(obs_data_get_string(settings, "lol_dashboard.minimap_cover_path"));
		if (!obs_data_has_user_value(settings, "lol_dashboard.use_custom_minimap_cover") &&
		    !custom_cover_path.isEmpty()) {
			use_custom_minimap_cover_ = true;
			obs_data_set_bool(settings, "lol_dashboard.use_custom_minimap_cover", true);
		}
		minimap_cover_width_percent_ =
			int(obs_data_get_int(settings, "lol_dashboard.minimap_cover_width_percent"));
		minimap_cover_height_percent_ =
			int(obs_data_get_int(settings, "lol_dashboard.minimap_cover_height_percent"));
		minimap_cover_scale_percent_ =
			int(obs_data_get_int(settings, "lol_dashboard.minimap_cover_scale_percent"));
		minimap_cover_translate_x_percent_ =
			int(obs_data_get_int(settings, "lol_dashboard.minimap_cover_translate_x_percent"));
		minimap_cover_translate_y_percent_ =
			int(obs_data_get_int(settings, "lol_dashboard.minimap_cover_translate_y_percent"));
		minimap_cover_alpha_padding_percent_ =
			int(obs_data_get_int(settings, "lol_dashboard.minimap_cover_alpha_padding_percent"));
		load_minimap_cover(use_custom_minimap_cover_ ? custom_cover_path : QString{});
		camera_visibility_.sync(source_, camera_source_uuid_);
		const int left = advanced_positioning_ ? int(obs_data_get_int(settings, "lol_dashboard.frame_left"))
						       : 0;
		const int top = advanced_positioning_ ? int(obs_data_get_int(settings, "lol_dashboard.frame_top")) : 0;
		window_ = std::clamp(int(obs_data_get_int(settings, "lol_dashboard.window")), 1, 60);
		regions_ = {obs_data_get_bool(settings, "lol_dashboard.show_intensity"),
			    obs_data_get_bool(settings, "lol_dashboard.show_keys"),
			    obs_data_get_bool(settings, "lol_dashboard.show_mouse_activity")};
		theme_ = {obs_color(uint32_t(obs_data_get_int(settings, "activity.inactive_color"))),
			  obs_color(uint32_t(obs_data_get_int(settings, "activity.active_color"))),
			  obs_color(uint32_t(obs_data_get_int(settings, "activity.background_color")))};
		heatmap_ = {QString::fromUtf8(obs_data_get_string(settings, "lol_dashboard.heatmap_gradient")),
			    obs_color(uint32_t(obs_data_get_int(settings, "lol_dashboard.gradient_low"))),
			    obs_color(uint32_t(obs_data_get_int(settings, "lol_dashboard.gradient_middle"))),
			    obs_color(uint32_t(obs_data_get_int(settings, "lol_dashboard.gradient_high"))),
			    qreal(std::clamp(int(obs_data_get_int(settings, "lol_dashboard.hex_size")), 2, 100))};
		style_ = {std::clamp(int(obs_data_get_int(settings, "lol_dashboard.section_padding")), 0, 100),
			  std::clamp(int(obs_data_get_int(settings, "lol_dashboard.element_padding")), 0, 100),
			  std::clamp(int(obs_data_get_int(settings, "lol_dashboard.element_x_gap")), 0, 100),
			  std::clamp(int(obs_data_get_int(settings, "lol_dashboard.element_y_gap")), 0, 100),
			  std::clamp(int(obs_data_get_int(settings, "lol_dashboard.within_element_gap")), 0, 100),
			  QString::fromUtf8(obs_data_get_string(settings, "lol_dashboard.font_family")),
			  float(obs_data_get_double(settings, "lol_dashboard.font_optical_size")),
			  float(obs_data_get_double(settings, "lol_dashboard.font_weight")),
			  float(obs_data_get_double(settings, "lol_dashboard.font_width")),
			  float(obs_data_get_double(settings, "lol_dashboard.font_slant")),
			  std::clamp(int(obs_data_get_int(settings, "lol_dashboard.label_size")), 8, 100),
			  std::clamp(int(obs_data_get_int(settings, "lol_dashboard.number_size")), 8, 100),
			  std::clamp(int(obs_data_get_int(settings, "lol_dashboard.number_label_size")), 8, 100),
			  std::clamp(int(obs_data_get_int(settings, "lol_dashboard.button_label_size")), 8, 100)};
		reload();
		if (layout_)
			frame_ = {left, top, layout_->game.width, layout_->game.height};
	}

	void tick(float seconds)
	{
		if (game_config_watcher_.changed(seconds))
			reload();
		const bool game_is_frontmost = uiohook::league_game_is_frontmost();
		if (auto_reset_at_game_start_ && game_start_watcher_.consume_start(game_start_cursor_))
			reset_statistics();
		game_visible_ = always_visible_ || game_is_frontmost;
		camera_mode_visible_ = show_camera_;
		league_capture_switcher::switch_captures(game_capture_source_, client_capture_source_,
							 game_is_frontmost);
		if (!layout_)
			return;
		const auto panels = panel_rectangles();
		if (camera_mode_visible_ && panels.camera_visible)
			camera_visibility_.fit_to_panel(panels.camera_mask.left(), panels.camera_mask.top(),
							panels.camera_mask.width(), panels.camera_mask.height(),
							panels.camera.left(), panels.camera.top(),
							panels.camera.width(), panels.camera.height());
		const QRect heatmap_bounds = qrect(panels.heatmap)
						     .adjusted(style_.section_padding, style_.section_padding,
							       -style_.section_padding, -style_.section_padding);
		visuals_.configure(theme_, heatmap_, regions_, window_, frame_, heatmap_bounds, style_);
		if (!game_is_frontmost) {
			discard_backlog_ = true;
			return;
		}
		std::vector<input_data::trace_event> events;
		input_data::button_map<uint16_t> keyboard, mouse;
		input_broker::consume(target(), cursor_, discard_backlog_, events, keyboard, mouse);
		visuals_.consume(events, keyboard, mouse);
	}

	void draw(gs_effect_t *effect)
	{
		if (!layout_ || (!game_visible_ && !camera_mode_visible_))
			return;
		const int width = layout_->game.width, height = layout_->game.height;
		QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
		image.fill(Qt::transparent);
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing);
		const auto panels = panel_rectangles();
		if (camera_mode_visible_ && panels.camera_visible)
			painter.fillRect(qrect(panels.camera_mask), camera_background_color_);
		if (game_visible_) {
			visuals_.draw(painter, qrect(panels.header), qrect(panels.heatmap), qrect(panels.summary),
				      qrect(panels.keys), panels.right_aligned);
			if (show_minimap_cover_ && !minimap_cover_.isNull() && !panels.minimap_cover_mask.isEmpty()) {
				painter.save();
				painter.setClipRect(qrect(panels.minimap_cover_mask));
				painter.drawImage(qrect(panels.minimap_cover), minimap_cover_);
				painter.restore();
			}
		}
		if (!texture_ || texture_width_ != width || texture_height_ != height) {
			gs_texture_destroy(texture_);
			texture_ = gs_texture_create(width, height, GS_RGBA, 1, nullptr, GS_DYNAMIC);
			texture_width_ = width;
			texture_height_ = height;
		}
		if (!texture_)
			return;
		gs_texture_set_image(texture_, image.constBits(), uint32_t(image.bytesPerLine()), false);
		gs_blend_state_push();
		gs_enable_blending(true);
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), texture_);
		gs_draw_sprite(texture_, 0, width, height);
		gs_blend_state_pop();
	}

	uint32_t width() const { return layout_ ? uint32_t(layout_->game.width) : 1; }
	uint32_t height() const { return layout_ ? uint32_t(layout_->game.height) : 1; }
	obs_source_t *source() const { return source_; }
	bool uses_advanced_positioning() const { return advanced_positioning_; }
	void reload()
	{
		QFile file(path_);
		if (!file.open(QIODevice::ReadOnly))
			return;
		const auto parsed = league_safe_area::parse_game_config(file.readAll().toStdString());
		if (parsed.value)
			layout_ = league_safe_area::make_model(*parsed.value);
	}
	void auto_detect()
	{
		for (const QString &candidate : game_config_candidates()) {
			QFile file(candidate);
			if (!file.open(QIODevice::ReadOnly) ||
			    !league_safe_area::parse_game_config(file.readAll().toStdString()).value)
				continue;
			obs_data_t *settings = obs_source_get_settings(source_);
			obs_data_set_string(settings, path_key, candidate.toUtf8().constData());
			obs_source_update(source_, settings);
			obs_data_release(settings);
			return;
		}
	}
	void reset_statistics() { visuals_.reset(); }
	void activate() {}
	void deactivate() {}
	void auto_link_captures()
	{
		obs_data_t *settings = obs_source_get_settings(source_);
		if (league_capture_switcher::auto_link(settings))
			obs_source_update(source_, settings);
		obs_data_release(settings);
	}

private:
	static QStringList game_config_candidates()
	{
		QStringList candidates;
#ifdef __APPLE__
		candidates << "/Applications/League of Legends.app/Contents/LoL/Config/game.cfg"
			   << QDir::homePath() + "/Applications/League of Legends.app/Contents/LoL/Config/game.cfg";
#endif
		candidates.removeDuplicates();
		return candidates;
	}
	input_broker::target target() const
	{
		input_broker::target result;
		result.rectangle_enabled = true;
		result.rectangle_left = frame_.left();
		result.rectangle_top = frame_.top();
		result.rectangle_right = frame_.right();
		result.rectangle_bottom = frame_.bottom();
		return result;
	}
	std::optional<double> camera_aspect() const;
	lol_dashboard_panels panel_rectangles() const;
	void load_minimap_cover(const QString &custom_path)
	{
		QImage image;
		if (!custom_path.isEmpty()) {
			QImageReader reader(custom_path);
			reader.setAutoTransform(true);
			image = reader.read();
		}
		if (!image.isNull()) {
			minimap_cover_ = image;
			return;
		}
		char *default_path = obs_module_file("images/minimap-cover.png");
		if (default_path) {
			minimap_cover_.load(QString::fromUtf8(default_path));
			bfree(default_path);
		}
	}
	void render_camera(const QRect &mask, const QRect &bounds);

	obs_source_t *source_{};
	QString path_;
	QRect frame_{0, 0, 1920, 1080};
	int window_{60};
	bool advanced_positioning_{}, always_visible_{}, game_visible_{}, camera_mode_visible_{}, show_camera_{},
		show_minimap_cover_{true}, use_custom_minimap_cover_{}, camera_source_initialized_{},
		auto_reset_at_game_start_{true};
	std::string game_capture_source_, client_capture_source_;
	std::string camera_source_uuid_;
	int camera_width_percent_{133}, camera_height_percent_{100}, camera_scale_percent_{100};
	int camera_translate_x_percent_{}, camera_translate_y_percent_{}, minimap_cover_width_percent_{100},
		minimap_cover_height_percent_{100}, minimap_cover_scale_percent_{100},
		minimap_cover_translate_x_percent_{}, minimap_cover_translate_y_percent_{},
		minimap_cover_alpha_padding_percent_{};
	QColor camera_background_color_{26, 26, 26, 255};
	lol_dashboard_theme theme_;
	lol_dashboard_heatmap heatmap_;
	lol_dashboard_regions regions_;
	lol_dashboard_style style_;
	lol_dashboard_game_start_watcher game_start_watcher_;
	uint64_t game_start_cursor_{};
	QImage minimap_cover_;
	lol_dashboard_game_config_watcher game_config_watcher_;
	lol_dashboard_camera_visibility camera_visibility_;
	std::optional<league_safe_area::model> layout_;
	lol_dashboard_visuals visuals_;
	uint64_t cursor_{};
	bool discard_backlog_{};
	gs_texture_t *texture_{};
	gs_texrender_t *camera_texture_{};
	int texture_width_{}, texture_height_{};
	obs_hotkey_id reset_hotkey_{OBS_INVALID_HOTKEY_ID};
};

#include "sources/dashboard/presentation/lol_properties.inc"
#include "sources/dashboard/rendering/lol_camera_render.inc"
} // namespace

void register_lol_performance_dashboard_source()
{
	obs_source_info info{};
	info.id = source_id;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = [](void *) {
		return obs_module_text("LoLPerformanceDashboard");
	};
	info.create = [](obs_data_t *settings, obs_source_t *source) {
		return static_cast<void *>(new dashboard_source(source, settings));
	};
	info.destroy = [](void *data) {
		delete static_cast<dashboard_source *>(data);
	};
	info.update = [](void *data, obs_data_t *settings) {
		static_cast<dashboard_source *>(data)->update(settings);
	};
	info.video_tick = [](void *data, float seconds) {
		static_cast<dashboard_source *>(data)->tick(seconds);
	};
	info.video_render = [](void *data, gs_effect_t *effect) {
		static_cast<dashboard_source *>(data)->draw(effect);
	};
	info.activate = [](void *data) {
		static_cast<dashboard_source *>(data)->activate();
	};
	info.deactivate = [](void *data) {
		static_cast<dashboard_source *>(data)->deactivate();
	};
	info.get_width = [](void *data) {
		return static_cast<dashboard_source *>(data)->width();
	};
	info.get_height = [](void *data) {
		return static_cast<dashboard_source *>(data)->height();
	};
	info.get_properties = properties;
	info.get_defaults = defaults;
	obs_register_source(&info);
}
} // namespace sources
