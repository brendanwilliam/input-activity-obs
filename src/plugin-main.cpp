/*
 * Input Activity for OBS
 * Copyright (C) 2026 Brendan Keane
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "hook/uiohook_helper.hpp"
#include "sources/activity_sources.hpp"

#include <obs-module.h>
#include <plugin-support.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void)
{
	sources::register_activity_sources();
	sources::register_league_safe_area_source();
	uiohook::start();
	blog(LOG_INFO, "[input-activity] loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	uiohook::stop();
	blog(LOG_INFO, "[input-activity] unloaded");
}
