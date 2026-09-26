/*
Focus Source
Copyright (C) 2026 focus-source

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <plugin-support.h>

#ifdef _WIN32
#include "focus-dock.hpp"

#include <obs-frontend-api.h>
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

#ifdef _WIN32
static FocusDock *dock_widget = nullptr;

static void frontend_save(obs_data_t *save_data, bool saving, void *private_data)
{
	static_cast<FocusDock *>(private_data)->save(save_data, saving);
}

bool obs_module_load(void)
{
	auto *dock = new FocusDock();
	if (!obs_frontend_add_dock_by_id("focus_source_dock", obs_module_text("Dock.Title"), dock)) {
		delete dock;
		obs_log(LOG_ERROR, "failed to add the focus dock");
		return false;
	}

	dock_widget = dock;
	obs_frontend_add_save_callback(frontend_save, dock);
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	if (dock_widget) {
		obs_frontend_remove_save_callback(frontend_save, dock_widget);
		dock_widget = nullptr;
	}
	obs_frontend_remove_dock("focus_source_dock");
	obs_log(LOG_INFO, "plugin unloaded");
}
#else
bool obs_module_load(void)
{
	obs_log(LOG_INFO, "focus-source only toggles sources on Windows (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}
#endif
