
bool unified_mode_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const std::string mode = obs_data_get_string(settings, "input_activity.mode");
	obs_property_set_visible(obs_properties_get(props, "input_activity.live_keys"), mode == "live_keys");
	obs_property_set_visible(obs_properties_get(props, "input_activity.mouse_activity"), mode == "mouse_activity");
	obs_property_set_visible(obs_properties_get(props, "input_activity.input_intensity"),
				 mode == "input_intensity");
	obs_property_set_visible(obs_properties_get(props, "input_activity.input_statistics"),
				 mode == "input_statistics");
	set_advanced_properties_visibility(props, obs_data_get_bool(settings, "input_activity.show_advanced_settings"));
	return true;
}

bool advanced_settings_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	set_advanced_properties_visibility(props, obs_data_get_bool(settings, "input_activity.show_advanced_settings"));
	return true;
}

obs_properties_t *unified_properties(void *data)
{
	auto *p = obs_properties_create();
	auto *mode = obs_properties_add_list(p, "input_activity.mode", obs_module_text("InputActivity.Mode"),
					     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(mode, obs_module_text("LiveKeys"), "live_keys");
	obs_property_list_add_string(mode, obs_module_text("MouseActivity"), "mouse_activity");
	obs_property_list_add_string(mode, obs_module_text("InputIntensity"), "input_intensity");
	obs_property_list_add_string(mode, obs_module_text("InputStatistics"), "input_statistics");
	obs_property_set_modified_callback(mode, unified_mode_changed);
	auto *general = add_group(p, "input_activity.general", obs_module_text("Preferences.General"));
	add_common_properties(general);
	auto *advanced = obs_properties_add_bool(general, "input_activity.show_advanced_settings",
						 obs_module_text("Preferences.ShowAdvanced"));
	obs_property_set_modified_callback(advanced, advanced_settings_changed);
	add_common_properties(p);
	auto *unified = static_cast<unified_source *>(data);
	auto *live_group = obs_properties_add_group(p, "input_activity.live_keys", obs_module_text("LiveKeys"),
						    OBS_GROUP_NORMAL, keys_properties_impl(data, false));
	auto *mouse_group = obs_properties_add_group(
		p, "input_activity.mouse_activity", obs_module_text("MouseActivity"), OBS_GROUP_NORMAL,
		mouse_properties_impl(unified ? unified->mouse() : nullptr, false, "mouse_activity"));
	auto *intensity_group = obs_properties_add_group(p, "input_activity.input_intensity",
							 obs_module_text("InputIntensity"), OBS_GROUP_NORMAL,
							 intensity_properties_impl(data, false, "input_intensity"));
	auto *statistics_group = obs_properties_add_group(p, "input_activity.input_statistics",
							  obs_module_text("InputStatistics"), OBS_GROUP_NORMAL,
							  statistics_properties_impl(data, false, "statistics"));
	if (unified) {
		unified->set_properties_visibility(p);
	} else {
		obs_property_set_visible(live_group, true);
		obs_property_set_visible(mouse_group, false);
		obs_property_set_visible(intensity_group, false);
		obs_property_set_visible(statistics_group, false);
		set_advanced_properties_visibility(p, false);
	}
	return p;
}
} // namespace

void register_activity_sources()
{
	register_source<unified_source>("input-activity", unified_properties);
}
} // namespace sources
