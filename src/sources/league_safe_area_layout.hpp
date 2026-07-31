#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace league_safe_area {
struct rect {
	double left{};
	double top{};
	double right{};
	double bottom{};
};

struct config {
	int width{};
	int height{};
	int window_mode{};
	double minimap_scale{};
	bool flip_minimap{};
	int chat_scale{};
	bool team_frames_left{};
};

struct model {
	config game;
	std::vector<rect> exclusions;
	std::vector<rect> safe_regions;
};

struct parse_result {
	std::optional<config> value;
	std::string error;
};

parse_result parse_game_config(std::string_view contents);
model make_model(const config &game);
bool contains(const rect &outer, const rect &inner);
} // namespace league_safe_area
