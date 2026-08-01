#include "sources/league_safe_area_layout.hpp"
#include "sources/lol_performance_dashboard_layout.hpp"

#include <cmath>
#include <string>

using namespace league_safe_area;

namespace {
std::string config_text(double global, double minimap, int chat, int flip, int team, int width = 2560,
			int height = 1440)
{
	return "[General]\nWidth=" + std::to_string(width) + "\nHeight=" + std::to_string(height) +
	       "\nWindowMode=2\n[HUD]\nGlobalScale=" + std::to_string(global) +
	       "\nPracticeToolScale=0\nMinimapScale=" + std::to_string(minimap) +
	       "\nFlipMiniMap=" + std::to_string(flip) + "\nChatScale=" + std::to_string(chat) +
	       "\nShowTeamFramesOnLeft=" + std::to_string(team) + "\n";
}

bool require(bool value)
{
	return value;
}
} // namespace

int main()
{
	auto minimum = parse_game_config(config_text(0, 0, 0, 0, 0));
	auto maximum = parse_game_config(config_text(1, 3, 100, 0, 0));
	if (!require(minimum.value && maximum.value))
		return 1;
	auto min_model = make_model(*minimum.value);
	auto max_model = make_model(*maximum.value);
	if (!require(std::abs(min_model.exclusions[0].left - 0.290) < 0.000001) ||
	    !require(std::abs(min_model.exclusions[0].top - 0.880) < 0.000001) ||
	    !require(std::abs(min_model.exclusions[0].right - 0.650) < 0.000001) ||
	    !require(max_model.exclusions[1].left < min_model.exclusions[1].left) ||
	    !require(max_model.exclusions[0].left < min_model.exclusions[0].left) ||
	    !require(max_model.exclusions[0].top < min_model.exclusions[0].top) ||
	    !require(max_model.exclusions[4].left < min_model.exclusions[4].left) ||
	    !require(std::abs(min_model.exclusions[2].right - 0.125) < 0.000001) ||
	    !require(std::abs(max_model.exclusions[2].right - 0.192) < 0.000001) ||
	    !require(std::abs(max_model.exclusions[3].left - 0.796) < 0.000001) ||
	    !require(std::abs(max_model.exclusions[4].bottom - max_model.exclusions[4].top - 0.220) < 0.000001))
		return 1;
	auto flipped = parse_game_config(config_text(1, 3, 100, 1, 1));
	if (!require(static_cast<bool>(flipped.value)))
		return 1;
	auto flipped_model = make_model(*flipped.value);
	if (!require(flipped_model.exclusions[1].left == 0.0) ||
	    !require(std::abs(flipped_model.exclusions[1].right - 0.216) < 0.000001) ||
	    !require(flipped_model.exclusions[4].left == 0.0))
		return 1;
	for (const auto &safe : flipped_model.safe_regions) {
		if (!require(!contains(flipped_model.exclusions[1], safe)) ||
		    !require(!contains(flipped_model.exclusions[3], safe)))
			return 1;
	}
	auto mixed_scales = parse_game_config(config_text(0, 3, 27, 0, 0));
	if (!require(static_cast<bool>(mixed_scales.value)))
		return 1;
	auto mixed_model = make_model(*mixed_scales.value);
	if (!require(std::abs(mixed_model.exclusions[2].right - min_model.exclusions[2].right) < 0.000001) ||
	    !require(std::abs(mixed_model.exclusions[4].left - max_model.exclusions[4].left) < 0.000001))
		return 1;
	auto default_panels = sources::lol_dashboard_panel_rectangles(min_model, {}, {});
	auto max_minimap_panels = sources::lol_dashboard_panel_rectangles(max_model, {}, {});
	if (!require(!default_panels.camera_visible) ||
	    !require(default_panels.heatmap.bottom() < min_model.game.height) ||
	    !require(default_panels.minimap_cover_mask.right() == min_model.game.width - 1) ||
	    !require(default_panels.minimap_cover_mask.bottom() == min_model.game.height - 1) ||
	    !require(max_minimap_panels.minimap_cover_mask.width() > default_panels.minimap_cover_mask.width()) ||
	    !require(max_minimap_panels.minimap_cover_mask.height() > default_panels.minimap_cover_mask.height()))
		return 1;
	sources::lol_dashboard_camera_layout camera{true, 16.0 / 9.0, 67, 100, 100, 0, 0};
	auto camera_panels = sources::lol_dashboard_panel_rectangles(min_model, camera, {});
	if (!require(camera_panels.camera_visible) || !require(camera_panels.camera_mask.left() >= 0) ||
	    !require(camera_panels.camera_mask.bottom() < min_model.game.height) ||
	    !require(camera_panels.camera.width() >= camera_panels.camera_mask.width()) ||
	    !require(camera_panels.camera.height() >= camera_panels.camera_mask.height()) ||
	    !require(camera_panels.heatmap.top() >= camera_panels.header.bottom()) ||
	    !require(camera_panels.summary.left() == camera_panels.heatmap.left()) ||
	    !require(camera_panels.right_aligned))
		return 1;
	sources::lol_dashboard_image_layout minimap_cover{1.0, 50, 75, 150, 20, -10};
	auto cover_panels = sources::lol_dashboard_panel_rectangles(min_model, {}, minimap_cover);
	auto centered_cover_panels = sources::lol_dashboard_panel_rectangles(min_model, {}, {1.0, 50, 75, 150, 0, 0});
	if (!require(cover_panels.minimap_cover.width() >= cover_panels.minimap_cover_mask.width()) ||
	    !require(cover_panels.minimap_cover.height() >= cover_panels.minimap_cover_mask.height()) ||
	    !require(cover_panels.minimap_cover.left() > centered_cover_panels.minimap_cover.left()) ||
	    !require(cover_panels.minimap_cover.top() < centered_cover_panels.minimap_cover.top()))
		return 1;
	auto flipped_panels = sources::lol_dashboard_panel_rectangles(flipped_model, camera, {});
	if (!require(flipped_panels.camera_mask.right() < flipped_model.game.width) ||
	    !require(flipped_panels.minimap_cover_mask.left() == 0) ||
	    !require(flipped_panels.heatmap.right() < flipped_model.game.width))
		return 1;
	auto invalid = parse_game_config("[General]\nWidth=2560\n");
	if (!require(!invalid.value))
		return 1;
	auto retained = min_model;
	if (invalid.value)
		retained = make_model(*invalid.value);
	if (!require(retained.game.width == 2560 && retained.game.height == 1440))
		return 1;
	auto embedded_game = parse_game_config(config_text(0.5, 1.5, 27, 0, 0, 2560, 1440));
	return require(embedded_game.value && make_model(*embedded_game.value).game.width == 2560) ? 0 : 1;
}
