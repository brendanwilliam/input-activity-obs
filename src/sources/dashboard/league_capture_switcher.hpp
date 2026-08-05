#pragma once

#include <string>
#include <vector>

struct obs_data;
typedef struct obs_data obs_data_t;

namespace sources::league_capture_switcher {
inline constexpr const char *game_source_key = "lol_dashboard.game_capture_source";
inline constexpr const char *client_source_key = "lol_dashboard.client_capture_source";

struct source_option {
	std::string name;
	std::string uuid;
};

std::vector<source_option> capture_sources();
bool auto_link(obs_data_t *settings);
void switch_captures(const std::string &game_source_uuid, const std::string &client_source_uuid,
		     bool game_is_frontmost);
} // namespace sources::league_capture_switcher
