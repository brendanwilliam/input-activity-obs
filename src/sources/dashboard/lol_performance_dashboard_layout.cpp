#include "lol_performance_dashboard_layout.hpp"

#include <algorithm>
#include <cmath>

namespace sources {
namespace {
constexpr int horizontal_padding = 40;
constexpr int vertical_top_padding = 20;
constexpr int vertical_bottom_padding = 40;
constexpr int panel_gap = 20;

lol_dashboard_rect scaled(const league_safe_area::rect &rect, int width, int height)
{
	return {int(std::lround(rect.left * width)), int(std::lround(rect.top * height)),
		int(std::lround((rect.right - rect.left) * width)),
		int(std::lround((rect.bottom - rect.top) * height))};
}

lol_dashboard_rect cover(const lol_dashboard_rect &bounds, double aspect, double scale)
{
	if (bounds.width() < 1 || bounds.height() < 1 || aspect <= 0.0)
		return {};
	const double width = std::max<double>(bounds.width(), bounds.height() * aspect) * scale;
	const double height = width / aspect;
	return {0, 0, std::max(1, int(std::lround(width))), std::max(1, int(std::lround(height)))};
}

lol_dashboard_rect fit(const lol_dashboard_rect &bounds, double aspect, double scale)
{
	if (bounds.width() < 1 || bounds.height() < 1 || aspect <= 0.0)
		return {};
	const double width = std::min<double>(bounds.width(), bounds.height() * aspect) * scale;
	const double height = width / aspect;
	return {0, 0, std::max(1, int(std::lround(width))), std::max(1, int(std::lround(height)))};
}

lol_dashboard_rect anchored_lower_corner(lol_dashboard_rect rect, const lol_dashboard_rect &bounds, bool left)
{
	rect.moveLeft(left ? bounds.left() : bounds.right() - rect.width() + 1);
	rect.moveTop(bounds.bottom() - rect.height() + 1);
	return rect;
}
} // namespace

lol_dashboard_panels lol_dashboard_panel_rectangles(const league_safe_area::model &layout,
						    const lol_dashboard_camera_layout &camera,
						    const lol_dashboard_image_layout &minimap_cover)
{
	const int width = layout.game.width;
	const int height = layout.game.height;
	const auto &player = layout.exclusions[0], &minimap = layout.exclusions[1];
	const auto &top_left = layout.exclusions[2], &top_right = layout.exclusions[3], &team = layout.exclusions[4];
	const bool minimap_left = layout.game.flip_minimap;
	const double side_left = minimap_left ? 0.0 : minimap.left;
	const double side_right = minimap_left ? minimap.right : 1.0;
	const double key_top = minimap_left ? top_left.bottom : top_right.bottom;
	const double key_bottom = layout.game.team_frames_left == minimap_left ? team.top : minimap.top;
	const league_safe_area::rect key{side_left, key_top, side_right, std::max(key_top, key_bottom)};
	const league_safe_area::rect mouse = minimap_left ? league_safe_area::rect{player.right, player.top, 1.0, 1.0}
							  : league_safe_area::rect{0.0, player.top, player.left, 1.0};
	const lol_dashboard_rect camera_anchor_bounds = scaled(mouse, width, height);
	const lol_dashboard_rect mouse_bounds = scaled(mouse, width, height)
							.adjusted(horizontal_padding, vertical_top_padding,
								  -horizontal_padding, -vertical_bottom_padding);
	const int heat_width = std::max(1, mouse_bounds.width() / 2);
	const int heat_height = std::max(1, int(std::lround(heat_width / (double(width) / std::max(1, height)))));
	const league_safe_area::rect header{top_left.right, 0.0, top_right.left, std::max(top_right.bottom, 0.12)};
	const lol_dashboard_rect header_bounds = scaled(header, width, height).adjusted(panel_gap, 0, -panel_gap, 0);

	lol_dashboard_panels result;
	result.header = header_bounds;
	result.keys = scaled(key, width, height).adjusted(panel_gap, panel_gap, -panel_gap, -panel_gap);
	result.right_aligned = !minimap_left;

	const lol_dashboard_rect cover_bounds = scaled(minimap, width, height);
	const lol_dashboard_rect cover_mask{
		cover_bounds.left(), cover_bounds.top(),
		std::max(1, cover_bounds.width() * std::clamp(minimap_cover.width_percent, 1, 200) / 100),
		std::max(1, cover_bounds.height() * std::clamp(minimap_cover.height_percent, 1, 200) / 100)};
	result.minimap_cover_mask = anchored_lower_corner(cover_mask, cover_bounds, !minimap_left);
	const int padding = std::min(
		std::min(result.minimap_cover_mask.width(), result.minimap_cover_mask.height()) / 2,
		int(std::lround(std::min(result.minimap_cover_mask.width(), result.minimap_cover_mask.height()) *
				std::clamp(minimap_cover.alpha_padding_percent, 0, 50) / 100.0)));
	const lol_dashboard_rect image_bounds =
		result.minimap_cover_mask.adjusted(padding, padding, -padding, -padding);
	result.minimap_cover = (minimap_cover.fit_within_mask ? fit : cover)(
		image_bounds, minimap_cover.aspect, std::clamp(minimap_cover.scale_percent, 1, 400) / 100.0);
	result.minimap_cover.moveLeft(image_bounds.left() + (image_bounds.width() - result.minimap_cover.width()) / 2);
	result.minimap_cover.moveTop(image_bounds.top() + (image_bounds.height() - result.minimap_cover.height()) / 2);
	result.minimap_cover.translate(
		cover_bounds.width() * std::clamp(minimap_cover.translate_x_percent, -200, 200) / 100,
		cover_bounds.height() * std::clamp(minimap_cover.translate_y_percent, -200, 200) / 100);

	const lol_dashboard_rect camera_bounds{
		camera_anchor_bounds.left(), camera_anchor_bounds.top(),
		std::max(1, camera_anchor_bounds.width() * std::clamp(camera.width_percent, 1, 200) / 200),
		std::max(1, cover_bounds.height() * std::clamp(camera.height_percent, 1, 200) / 100)};
	if (camera.enabled && camera.aspect > 0.0) {
		result.camera_mask = anchored_lower_corner(camera_bounds, camera_anchor_bounds, !minimap_left);
		result.camera =
			cover(result.camera_mask, camera.aspect, std::clamp(camera.scale_percent, 1, 400) / 100.0);
		result.camera.moveLeft(result.camera_mask.left() +
				       (result.camera_mask.width() - result.camera.width()) / 2);
		result.camera.moveTop(result.camera_mask.top() +
				      (result.camera_mask.height() - result.camera.height()) / 2);
		result.camera.translate(
			camera_anchor_bounds.width() * std::clamp(camera.translate_x_percent, -200, 200) / 100,
			cover_bounds.height() * std::clamp(camera.translate_y_percent, -200, 200) / 100);
		result.camera_visible = true;
		const int top = std::max(header_bounds.bottom() + 1, panel_gap);
		result.heatmap = {minimap_left ? width - panel_gap - heat_width : panel_gap, top, heat_width,
				  heat_height};
		result.summary = {result.heatmap.left(), result.heatmap.bottom() + panel_gap + 1, heat_width,
				  std::max(1, mouse_bounds.bottom() - result.heatmap.bottom() - panel_gap)};
		result.right_aligned = !minimap_left;
	} else {
		const int heat_top = std::max(0, mouse_bounds.bottom() - heat_height + 1);
		result.heatmap = {minimap_left ? mouse_bounds.right() - heat_width + 1 : mouse_bounds.left(), heat_top,
				  heat_width, heat_height};
		const int summary_width = std::max(1, mouse_bounds.width() / 4 - panel_gap);
		result.summary = {minimap_left ? result.heatmap.left() - summary_width - panel_gap
					       : result.heatmap.right() + panel_gap + 1,
				  mouse_bounds.top(), summary_width, mouse_bounds.height()};
	}
	return result;
}

} // namespace sources
