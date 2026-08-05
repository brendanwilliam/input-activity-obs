#include "sources/heatmap/lol_settings.hpp"

#include "sources/heatmap/lol_geometry.hpp"

#include <algorithm>
#include <mutex>
#include <obs-module.h>

extern "C" {
#include <util/bmem.h>
#include <util/config-file.h>
}

namespace sources::lol_heatmap {
namespace {
constexpr const char *file_name = "heatmap.ini";
constexpr const char *section = "geometry";
constexpr const char *radius_key = "radius_percent";

struct settings {
	std::mutex mutex;
	bool loaded{};
	bool migrated{};
	double radius{default_radius_percent};
};

settings &state()
{
	static settings value;
	return value;
}

void load(settings &value)
{
	if (value.loaded)
		return;
	value.loaded = true;
	char *path = obs_module_config_path(file_name);
	if (!path)
		return;
	config_t *config{};
	if (config_open(&config, path, CONFIG_OPEN_EXISTING) == CONFIG_SUCCESS) {
		if (config_has_user_value(config, section, radius_key)) {
			value.radius = std::clamp(config_get_double(config, section, radius_key), 0.1, 100.0);
			value.migrated = true;
		}
		config_close(config);
	}
	bfree(path);
}

void save(const settings &value)
{
	char *path = obs_module_config_path(file_name);
	if (!path)
		return;
	config_t *config{};
	if (config_open(&config, path, CONFIG_OPEN_ALWAYS) == CONFIG_SUCCESS) {
		config_set_double(config, section, radius_key, value.radius);
		config_save_safe(config, ".tmp", ".bak");
		config_close(config);
	}
	bfree(path);
}
} // namespace

double radius_percent()
{
	settings &value = state();
	std::lock_guard lock(value.mutex);
	load(value);
	return value.radius;
}

void set_radius_percent(double radius)
{
	settings &value = state();
	std::lock_guard lock(value.mutex);
	load(value);
	value.radius = std::clamp(radius, 0.1, 100.0);
	value.migrated = true;
	save(value);
}

void migrate_legacy_radius(int legacy_radius_pixels, int content_width_pixels)
{
	if (content_width_pixels < 1)
		return;
	settings &value = state();
	std::lock_guard lock(value.mutex);
	load(value);
	if (value.migrated)
		return;
	value.radius = std::clamp(100.0 * std::max(1, legacy_radius_pixels) / content_width_pixels, 0.1, 100.0);
	value.migrated = true;
	save(value);
}

} // namespace sources::lol_heatmap
