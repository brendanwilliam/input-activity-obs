#include "league_capture_switcher.hpp"

#include <algorithm>
#include <cctype>
#include <obs-module.h>

namespace sources::league_capture_switcher {
namespace {
constexpr const char *screen_capture_id = "screen_capture";
constexpr const char *game_application_id = "com.riotgames.LeagueofLegends.GameClient";
constexpr const char *client_application_id = "com.riotgames.LeagueofLegends.LeagueClientUx";

bool is_screen_capture(const obs_source_t *source)
{
	return std::string(obs_source_get_id(source)) == screen_capture_id;
}

std::string application_id(obs_source_t *source)
{
	obs_data_t *settings = obs_source_get_settings(source);
	const std::string id = obs_data_get_string(settings, "application");
	obs_data_release(settings);
	return id;
}

bool source_option_callback(void *data, obs_source_t *source)
{
	auto *options = static_cast<std::vector<source_option> *>(data);
	if (is_screen_capture(source))
		options->push_back({obs_source_get_name(source), obs_source_get_uuid(source)});
	return true;
}

struct auto_link_candidates {
	std::vector<source_option> game;
	std::vector<source_option> client;
};

bool auto_link_callback(void *data, obs_source_t *source)
{
	if (!is_screen_capture(source))
		return true;
	auto *candidates = static_cast<auto_link_candidates *>(data);
	const source_option option{obs_source_get_name(source), obs_source_get_uuid(source)};
	const std::string id = application_id(source);
	if (id == game_application_id)
		candidates->game.push_back(option);
	else if (id == client_application_id)
		candidates->client.push_back(option);
	return true;
}

const source_option *best_candidate(const std::vector<source_option> &candidates, const char *kind)
{
	if (candidates.empty())
		return nullptr;
	const auto match = [kind](const source_option &candidate) {
		std::string name = candidate.name;
		std::transform(name.begin(), name.end(), name.begin(),
			       [](unsigned char value) { return std::tolower(value); });
		return name.find("lol") != std::string::npos && name.find(kind) != std::string::npos;
	};
	const auto preferred = std::find_if(candidates.begin(), candidates.end(), match);
	return preferred != candidates.end() ? &*preferred : &candidates.front();
}

struct visibility_change {
	obs_source_t *source;
	bool visible;
};

bool set_source_visibility(obs_scene_t *, obs_sceneitem_t *item, void *data)
{
	auto *change = static_cast<visibility_change *>(data);
	if (obs_sceneitem_get_source(item) == change->source)
		obs_sceneitem_set_visible(item, change->visible);
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, set_source_visibility, data);
	return true;
}

bool set_scene_visibility(void *data, obs_source_t *scene_source)
{
	if (obs_scene_t *scene = obs_scene_from_source(scene_source))
		obs_scene_enum_items(scene, set_source_visibility, data);
	return true;
}

void set_visible_in_all_scenes(const std::string &source_uuid, bool visible)
{
	obs_source_t *source = obs_get_source_by_uuid(source_uuid.c_str());
	if (!source)
		return;
	visibility_change change{source, visible};
	obs_enum_scenes(set_scene_visibility, &change);
	obs_source_release(source);
}
} // namespace

std::vector<source_option> capture_sources()
{
	std::vector<source_option> options;
	obs_enum_sources(source_option_callback, &options);
	std::sort(options.begin(), options.end(),
		  [](const source_option &left, const source_option &right) { return left.name < right.name; });
	return options;
}

bool auto_link(obs_data_t *settings)
{
	auto_link_candidates candidates;
	obs_enum_sources(auto_link_callback, &candidates);
	const source_option *game = best_candidate(candidates.game, "game");
	const source_option *client = best_candidate(candidates.client, "client");
	if (!game || !client)
		return false;
	obs_data_set_string(settings, game_source_key, game->uuid.c_str());
	obs_data_set_string(settings, client_source_key, client->uuid.c_str());
	return true;
}

void switch_captures(const std::string &game_source_uuid, const std::string &client_source_uuid, bool game_is_frontmost)
{
	if (game_source_uuid.empty() || client_source_uuid.empty())
		return;
	set_visible_in_all_scenes(game_source_uuid, game_is_frontmost);
	set_visible_in_all_scenes(client_source_uuid, !game_is_frontmost);
}
} // namespace sources::league_capture_switcher
