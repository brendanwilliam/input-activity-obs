#include "league_safe_area_layout.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>
#include <string>

namespace league_safe_area {
namespace {
constexpr rect player_hud{0.315, 0.755, 0.685, 1.0};
constexpr rect score_margin{0.385, 0.0, 0.615, 0.075};
constexpr rect toolbar_margin{0.0, 0.18, 0.052, 0.70};
constexpr rect team_frames_right{0.875, 0.165, 0.985, 0.655};
constexpr double chat_min_width = 0.180;
constexpr double chat_max_width = 0.345;
constexpr double chat_min_height = 0.155;
constexpr double chat_max_height = 0.355;
constexpr double minimap_min_width = 0.140;
constexpr double minimap_max_width = 0.275;
constexpr double minimap_min_height = 0.250;
constexpr double minimap_max_height = 0.485;

std::string trim(std::string value)
{
	const auto begin = value.find_first_not_of(" \t\r");
	if (begin == std::string::npos)
		return {};
	const auto end = value.find_last_not_of(" \t\r");
	return value.substr(begin, end - begin + 1);
}

std::optional<int> integer(const std::string &value)
{
	int result{};
	const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
	if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
		return std::nullopt;
	return result;
}

std::optional<double> decimal(const std::string &value)
{
	char *end{};
	const double result = std::strtod(value.c_str(), &end);
	if (!end || end != value.c_str() + value.size() || !std::isfinite(result))
		return std::nullopt;
	return result;
}

rect mirrored(rect value)
{
	return {1.0 - value.right, value.top, 1.0 - value.left, value.bottom};
}

rect lower_left(double width, double height)
{
	return {0.0, 1.0 - height, width, 1.0};
}

rect lower_right(double width, double height)
{
	return {1.0 - width, 1.0 - height, 1.0, 1.0};
}

bool overlaps(const rect &a, const rect &b)
{
	return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

std::vector<rect> subtract(const std::vector<rect> &regions, const rect &cut)
{
	std::vector<rect> result;
	for (const rect &region : regions) {
		if (!overlaps(region, cut)) {
			result.push_back(region);
			continue;
		}
		const rect overlap{std::max(region.left, cut.left), std::max(region.top, cut.top),
				   std::min(region.right, cut.right), std::min(region.bottom, cut.bottom)};
		const auto add = [&result](rect value) {
			if (value.left < value.right && value.top < value.bottom)
				result.push_back(value);
		};
		add({region.left, region.top, region.right, overlap.top});
		add({region.left, overlap.bottom, region.right, region.bottom});
		add({region.left, overlap.top, overlap.left, overlap.bottom});
		add({overlap.right, overlap.top, region.right, overlap.bottom});
	}
	return result;
}

double interpolate(double minimum, double maximum, double fraction)
{
	return minimum + (maximum - minimum) * fraction;
}
} // namespace

parse_result parse_game_config(std::string_view contents)
{
	std::optional<int> width, height, window_mode, chat_scale, flip_minimap, team_frames_left;
	std::optional<double> minimap_scale;
	std::istringstream input{std::string(contents)};
	std::string line;
	std::string section;
	while (std::getline(input, line)) {
		line = trim(line);
		if (line.empty() || line[0] == ';' || line[0] == '#')
			continue;
		if (line.front() == '[' && line.back() == ']') {
			section = line.substr(1, line.size() - 2);
			continue;
		}
		const auto equal = line.find('=');
		if (equal == std::string::npos)
			continue;
		const std::string key = trim(line.substr(0, equal));
		const std::string value = trim(line.substr(equal + 1));
		if (section == "General") {
			if (key == "Width")
				width = integer(value);
			else if (key == "Height")
				height = integer(value);
			else if (key == "WindowMode")
				window_mode = integer(value);
		} else if (section == "HUD") {
			if (key == "MinimapScale")
				minimap_scale = decimal(value);
			else if (key == "FlipMiniMap")
				flip_minimap = integer(value);
			else if (key == "ChatScale")
				chat_scale = integer(value);
			else if (key == "ShowTeamFramesOnLeft")
				team_frames_left = integer(value);
		}
	}
	if (!width || !height || !window_mode || !minimap_scale || !flip_minimap || !chat_scale || !team_frames_left)
		return {{}, "Waiting for all required [General] and [HUD] settings"};
	if (*width < 1 || *width > 16384 || *height < 1 || *height > 16384 || *window_mode < 0 || *window_mode > 3 ||
	    *minimap_scale < 0.0 || *minimap_scale > 3.0 || *chat_scale < 0 || *chat_scale > 100 ||
	    (*flip_minimap != 0 && *flip_minimap != 1) || (*team_frames_left != 0 && *team_frames_left != 1))
		return {{}, "game.cfg has out-of-range HUD settings"};
	return {{config{*width, *height, *window_mode, *minimap_scale, *flip_minimap == 1, *chat_scale,
			*team_frames_left == 1}},
		{}};
}

model make_model(const config &game)
{
	const double chat = static_cast<double>(game.chat_scale) / 100.0;
	const double minimap = game.minimap_scale / 3.0;
	const rect chat_rect = lower_left(interpolate(chat_min_width, chat_max_width, chat),
					  interpolate(chat_min_height, chat_max_height, chat));
	const rect minimap_rect = lower_right(interpolate(minimap_min_width, minimap_max_width, minimap),
					      interpolate(minimap_min_height, minimap_max_height, minimap));
	model result{game,
		     {player_hud, game.flip_minimap ? mirrored(minimap_rect) : minimap_rect, chat_rect,
		      game.team_frames_left ? mirrored(team_frames_right) : team_frames_right, score_margin,
		      toolbar_margin},
		     {{0.0, 0.0, 1.0, 1.0}}};
	for (const rect &exclusion : result.exclusions)
		result.safe_regions = subtract(result.safe_regions, exclusion);
	return result;
}

bool contains(const rect &outer, const rect &inner)
{
	return outer.left <= inner.left && outer.top <= inner.top && outer.right >= inner.right &&
	       outer.bottom >= inner.bottom;
}
} // namespace league_safe_area
