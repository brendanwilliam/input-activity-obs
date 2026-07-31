#include "activity_sources.hpp"

#include "../hook/uiohook_helper.hpp"
#include "../input/input_broker.hpp"
#include "league_safe_area_layout.hpp"
#include "lol_performance_dashboard_visuals.hpp"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QStringList>
#include <algorithm>
#include <array>
#include <memory>
#include <obs-module.h>

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

class dashboard_source {
public:
	dashboard_source(obs_source_t *source, obs_data_t *settings) : source_(source) { update(settings); }
	~dashboard_source()
	{
		if (texture_) {
			obs_enter_graphics();
			gs_texture_destroy(texture_);
			obs_leave_graphics();
		}
	}

	void update(obs_data_t *settings)
	{
		path_ = QString::fromUtf8(obs_data_get_string(settings, path_key));
		advanced_positioning_ = obs_data_get_bool(settings, "lol_dashboard.advanced_positioning");
		const int left = advanced_positioning_ ? int(obs_data_get_int(settings, "lol_dashboard.frame_left"))
						       : 0;
		const int top = advanced_positioning_ ? int(obs_data_get_int(settings, "lol_dashboard.frame_top")) : 0;
		window_ = std::clamp(int(obs_data_get_int(settings, "lol_dashboard.window")), 1, 60);
		theme_ = {obs_color(uint32_t(obs_data_get_int(settings, "activity.inactive_color"))),
			  obs_color(uint32_t(obs_data_get_int(settings, "activity.active_color"))),
			  obs_color(uint32_t(obs_data_get_int(settings, "activity.background_color")))};
		heatmap_ = {QString::fromUtf8(obs_data_get_string(settings, "lol_dashboard.heatmap_gradient")),
			    obs_color(uint32_t(obs_data_get_int(settings, "lol_dashboard.gradient_low"))),
			    obs_color(uint32_t(obs_data_get_int(settings, "lol_dashboard.gradient_middle"))),
			    obs_color(uint32_t(obs_data_get_int(settings, "lol_dashboard.gradient_high"))),
			    qreal(std::clamp(int(obs_data_get_int(settings, "lol_dashboard.hex_size")), 2, 100))};
		reload();
		if (layout_)
			frame_ = {left, top, layout_->game.width, layout_->game.height};
	}

	void tick(float)
	{
		visible_ = uiohook::league_game_is_running();
		if (!layout_ || !visible_)
			return;
		const auto panels = panel_rectangles();
		visuals_.configure(theme_, heatmap_, window_, frame_, panels.heatmap);
		std::vector<input_data::trace_event> events;
		input_data::button_map<uint16_t> keyboard, mouse;
		input_broker::consume(target(), cursor_, discard_backlog_, events, keyboard, mouse);
		visuals_.consume(events, keyboard, mouse);
	}

	void draw(gs_effect_t *effect)
	{
		if (!layout_ || !visible_)
			return;
		const int width = layout_->game.width, height = layout_->game.height;
		QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
		image.fill(Qt::transparent);
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing);
		const auto panels = panel_rectangles();
		visuals_.draw(painter, panels.header, panels.heatmap, panels.summary, panels.keys,
			      panels.right_aligned);
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

private:
	struct panels {
		QRect header, heatmap, summary, keys;
		bool right_aligned;
	};
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
	QRect scaled(const league_safe_area::rect &rect) const
	{
		return {int(std::lround(rect.left * width())), int(std::lround(rect.top * height())),
			int(std::lround((rect.right - rect.left) * width())),
			int(std::lround((rect.bottom - rect.top) * height()))};
	}
	panels panel_rectangles() const
	{
		const auto &player = layout_->exclusions[0], &minimap = layout_->exclusions[1];
		const auto &top_left = layout_->exclusions[2], &top_right = layout_->exclusions[3],
			   &team = layout_->exclusions[4];
		const bool minimap_left = layout_->game.flip_minimap;
		const double side_left = minimap_left ? 0.0 : minimap.left;
		const double side_right = minimap_left ? minimap.right : 1.0;
		const double key_top = minimap_left ? top_left.bottom : top_right.bottom;
		const double key_bottom = layout_->game.team_frames_left == minimap_left ? team.top : minimap.top;
		const league_safe_area::rect key{side_left, key_top, side_right, std::max(key_top, key_bottom)};
		const league_safe_area::rect mouse =
			minimap_left ? league_safe_area::rect{player.right, player.top, 1.0, 1.0}
				     : league_safe_area::rect{0.0, player.top, player.left, 1.0};
		QRect mouse_bounds = scaled(mouse).adjusted(20, 20, -20, -40);
		const int heat_width = mouse_bounds.width() / 2;
		const int summary_width = std::max(1, mouse_bounds.width() / 4 - 20);
		const double game_aspect = double(width()) / std::max(1u, height());
		const int heat_height = std::max(1, int(std::lround(heat_width / game_aspect)));
		const int heat_top = std::max(0, mouse_bounds.bottom() - heat_height + 1);
		const QRect heatmap(minimap_left ? mouse_bounds.right() - heat_width + 1 : mouse_bounds.left(),
				    heat_top, heat_width, heat_height);
		const QRect summary(minimap_left ? heatmap.left() - summary_width - 20 : heatmap.right() + 21,
				    mouse_bounds.top(), summary_width, mouse_bounds.height());
		const league_safe_area::rect header{top_left.right, 0.0, top_right.left,
						    std::max(top_right.bottom, 0.12)};
		return {scaled(header).adjusted(20, 0, -20, 0), heatmap, summary,
			scaled(key).adjusted(20, 20, -20, -20), !minimap_left};
	}

	obs_source_t *source_{};
	QString path_;
	QRect frame_{0, 0, 1920, 1080};
	int window_{60};
	bool advanced_positioning_{}, visible_{};
	lol_dashboard_theme theme_;
	lol_dashboard_heatmap heatmap_;
	std::optional<league_safe_area::model> layout_;
	lol_dashboard_visuals visuals_;
	uint64_t cursor_{};
	bool discard_backlog_{};
	gs_texture_t *texture_{};
	int texture_width_{}, texture_height_{};
};

bool auto_detect_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	static_cast<dashboard_source *>(data)->auto_detect();
	return true;
}
bool reload_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	static_cast<dashboard_source *>(data)->reload();
	return true;
}
bool reset_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	static_cast<dashboard_source *>(data)->reset_statistics();
	return true;
}
bool advanced_positioning_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	obs_property_set_visible(obs_properties_get(props, "lol_dashboard.frame"),
				 obs_data_get_bool(settings, "lol_dashboard.advanced_positioning"));
	return true;
}
bool heatmap_gradient_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool custom = std::string(obs_data_get_string(settings, "lol_dashboard.heatmap_gradient")) == "custom";
	for (const char *name :
	     {"lol_dashboard.gradient_low", "lol_dashboard.gradient_middle", "lol_dashboard.gradient_high"})
		obs_property_set_visible(obs_properties_get(props, name), custom);
	return true;
}
obs_properties_t *properties(void *data)
{
	auto *properties = obs_properties_create();
	obs_properties_add_path(properties, path_key, obs_module_text("LeagueSafeArea.GameCfg"), OBS_PATH_FILE,
				"game.cfg", nullptr);
	obs_properties_add_button2(properties, "lol_dashboard.auto_detect",
				   obs_module_text("LeagueSafeArea.AutoDetect"), auto_detect_clicked, data);
	obs_properties_add_button2(properties, "lol_dashboard.reload", obs_module_text("LeagueSafeArea.Reload"),
				   reload_clicked, data);
	obs_properties_add_button2(properties, "lol_dashboard.reset",
				   obs_module_text("LoLPerformanceDashboard.ResetStatistics"), reset_clicked, data);
	auto *advanced = obs_properties_add_bool(properties, "lol_dashboard.advanced_positioning",
						 obs_module_text("LoLPerformanceDashboard.AdvancedPositioning"));
	obs_property_set_modified_callback(advanced, advanced_positioning_changed);
	auto *frame = obs_properties_add_group(properties, "lol_dashboard.frame",
					       obs_module_text("LoLPerformanceDashboard.GameFrame"), OBS_GROUP_NORMAL,
					       obs_properties_create());
	auto *frame_properties = obs_property_group_content(frame);
	obs_properties_add_int(frame_properties, "lol_dashboard.frame_left",
			       obs_module_text("LoLPerformanceDashboard.Left"), -32768, 32767, 1);
	obs_properties_add_int(frame_properties, "lol_dashboard.frame_top",
			       obs_module_text("LoLPerformanceDashboard.Top"), -32768, 32767, 1);
	obs_properties_add_int_slider(properties, "lol_dashboard.window",
				      obs_module_text("LoLPerformanceDashboard.Window"), 1, 60, 1);
	auto *theme = obs_properties_add_group(properties, "lol_dashboard.theme",
					       obs_module_text("Preferences.Appearance"), OBS_GROUP_NORMAL,
					       obs_properties_create());
	auto *theme_properties = obs_property_group_content(theme);
	obs_properties_add_color_alpha(theme_properties, "activity.inactive_color",
				       obs_module_text("Activity.InactiveColor"));
	obs_properties_add_color_alpha(theme_properties, "activity.active_color",
				       obs_module_text("Activity.ActiveColor"));
	obs_properties_add_color_alpha(theme_properties, "activity.background_color",
				       obs_module_text("Activity.BackgroundColor"));
	auto *heatmap = obs_properties_add_group(properties, "lol_dashboard.heatmap",
						 obs_module_text("MouseActivity.HeatmapGradient"), OBS_GROUP_NORMAL,
						 obs_properties_create());
	auto *heatmap_properties = obs_property_group_content(heatmap);
	auto *gradient = obs_properties_add_list(heatmap_properties, "lol_dashboard.heatmap_gradient",
						 obs_module_text("MouseActivity.HeatmapGradient"), OBS_COMBO_TYPE_LIST,
						 OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(gradient, obs_module_text("MouseActivity.HeatmapGradient.Spectrum"), "spectrum");
	obs_property_list_add_string(gradient, obs_module_text("MouseActivity.HeatmapGradient.Lime"), "lime");
	obs_property_list_add_string(gradient, obs_module_text("MouseActivity.HeatmapGradient.Ocean"), "ocean");
	obs_property_list_add_string(gradient, obs_module_text("MouseActivity.HeatmapGradient.MatchTheme"), "theme");
	obs_property_list_add_string(gradient, obs_module_text("MouseActivity.HeatmapGradient.Custom"), "custom");
	obs_property_set_modified_callback(gradient, heatmap_gradient_changed);
	obs_properties_add_color_alpha(heatmap_properties, "lol_dashboard.gradient_low",
				       obs_module_text("MouseActivity.CustomGradientLow"));
	obs_properties_add_color_alpha(heatmap_properties, "lol_dashboard.gradient_middle",
				       obs_module_text("MouseActivity.CustomGradientMiddle"));
	obs_properties_add_color_alpha(heatmap_properties, "lol_dashboard.gradient_high",
				       obs_module_text("MouseActivity.CustomGradientHigh"));
	obs_properties_add_int_slider(heatmap_properties, "lol_dashboard.hex_size",
				      obs_module_text("MouseActivity.HexSize"), 2, 100, 1);
	if (data) {
		auto *settings = obs_source_get_settings(static_cast<dashboard_source *>(data)->source());
		heatmap_gradient_changed(heatmap_properties, nullptr, settings);
		obs_data_release(settings);
	}
	obs_property_set_visible(frame, data && static_cast<dashboard_source *>(data)->uses_advanced_positioning());
	return properties;
}
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
	info.get_width = [](void *data) {
		return static_cast<dashboard_source *>(data)->width();
	};
	info.get_height = [](void *data) {
		return static_cast<dashboard_source *>(data)->height();
	};
	info.get_properties = properties;
	info.get_defaults = [](obs_data_t *settings) {
		obs_data_set_default_bool(settings, "lol_dashboard.advanced_positioning", false);
		obs_data_set_default_int(settings, "lol_dashboard.window", 60);
		obs_data_set_default_int(settings, "activity.inactive_color", 0xff425e62);
		obs_data_set_default_int(settings, "activity.active_color", 0xff83c1dd);
		obs_data_set_default_int(settings, "activity.background_color", 0x00000000);
		obs_data_set_default_string(settings, "lol_dashboard.heatmap_gradient", "spectrum");
		obs_data_set_default_int(settings, "lol_dashboard.gradient_low", 0xffeb6325);
		obs_data_set_default_int(settings, "lol_dashboard.gradient_middle", 0xff15ccfa);
		obs_data_set_default_int(settings, "lol_dashboard.gradient_high", 0xff4444ef);
		obs_data_set_default_int(settings, "lol_dashboard.hex_size", 10);
	};
	obs_register_source(&info);
}
} // namespace sources
