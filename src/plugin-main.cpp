/*
 * Input Activity for OBS
 * Copyright (C) 2026 Brendan Keane
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "hook/uiohook_helper.hpp"
#include "sources/dashboard/presentation/lol_source.hpp"
#include "sources/game_report/presentation/lol_source.hpp"

#include <obs-module.h>
#include <plugin-support.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void)
{
	sources::register_lol_performance_dashboard_source();
	sources::register_lol_game_report_source();
	uiohook::start();
	blog(LOG_INFO, "[input-activity] loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	uiohook::stop();
	blog(LOG_INFO, "[input-activity] unloaded");
}
