bool mouse_region_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool custom = std::string(obs_data_get_string(settings, "mouse_activity.region")) == "custom";
	obs_property_set_visible(obs_properties_get(props, "mouse_activity.display"), !custom);
	obs_property_set_visible(obs_properties_get(props, "mouse_activity.region.left"), custom);
	obs_property_set_visible(obs_properties_get(props, "mouse_activity.region.top"), custom);
	obs_property_set_visible(obs_properties_get(props, "mouse_activity.region.right"), custom);
	obs_property_set_visible(obs_properties_get(props, "mouse_activity.region.bottom"), custom);
	obs_property_set_visible(obs_properties_get(props, "mouse_activity.region.help"), custom);
	return true;
}

bool mouse_options_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool heatmap = obs_data_get_bool(settings, "mouse_activity.show_heatmap");
	const bool distance = obs_data_get_bool(settings, "mouse_activity.show_distance");
	const bool summary = distance || obs_data_get_bool(settings, "mouse_activity.show_clicks");
	const bool custom_gradient = std::string(obs_data_get_string(settings, "mouse_activity.heatmap_gradient")) ==
				     "custom";
	for (const char *name : {"mouse_activity.heatmap_gradient", "mouse_activity.hex_size", "mouse_activity.opacity",
				 "mouse_activity.map"})
		obs_property_set_visible(obs_properties_get(props, name), heatmap);
	for (const char *name : {"mouse_activity.custom_gradient_low", "mouse_activity.custom_gradient_middle",
				 "mouse_activity.custom_gradient_high"})
		obs_property_set_visible(obs_properties_get(props, name), heatmap && custom_gradient);
	obs_property_set_visible(obs_properties_get(props, "mouse_activity.summary_position"), summary);
	obs_property_set_visible(obs_properties_get(props, "mouse_activity.distance_unit"), distance);
	obs_property_set_visible(
		obs_properties_get(props, "mouse_activity.mouse_dpi"),
		distance && std::string(obs_data_get_string(settings, "mouse_activity.distance_unit")) != "pixels");
	return true;
}

obs_properties_t *mouse_properties_impl(void *data, bool include_common, const char *title_prefix = nullptr)
{
	auto *p = obs_properties_create();
	if (include_common)
		add_common_properties(p);
	auto *content = add_group(p, "input_activity.mouse_activity.content", obs_module_text("Preferences.Content"));
	add_mode_title_properties(content, title_prefix);
	obs_properties_add_text(content, "mouse_activity.left_label", obs_module_text("MouseActivity.LeftLabel"),
				OBS_TEXT_DEFAULT);
	obs_properties_add_text(content, "mouse_activity.right_label", obs_module_text("MouseActivity.RightLabel"),
				OBS_TEXT_DEFAULT);
	obs_properties_add_text(content, "mouse_activity.middle_label", obs_module_text("MouseActivity.MiddleLabel"),
				OBS_TEXT_DEFAULT);
	auto *appearance =
		add_group(p, "input_activity.mouse_activity.appearance", obs_module_text("Preferences.Appearance"));
	auto *behavior =
		add_group(p, "input_activity.mouse_activity.behavior", obs_module_text("Preferences.Behavior"));
	auto *show_heatmap = obs_properties_add_bool(behavior, "mouse_activity.show_heatmap",
						     obs_module_text("MouseActivity.ShowHeatmap"));
	obs_property_set_modified_callback(show_heatmap, mouse_options_changed);
	obs_properties_add_bool(behavior, "mouse_activity.show_live_mouse",
				obs_module_text("MouseActivity.ShowLiveMouse"));
	auto *show_distance = obs_properties_add_bool(behavior, "mouse_activity.show_distance",
						      obs_module_text("MouseActivity.ShowDistance"));
	auto *show_clicks = obs_properties_add_bool(behavior, "mouse_activity.show_clicks",
						    obs_module_text("MouseActivity.ShowClicks"));
	obs_property_set_modified_callback(show_distance, mouse_options_changed);
	obs_property_set_modified_callback(show_clicks, mouse_options_changed);
	auto *summary_position = obs_properties_add_list(behavior, "mouse_activity.summary_position",
							 obs_module_text("MouseActivity.SummaryPosition"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(summary_position, obs_module_text("MouseActivity.SummaryPosition.Top"), "top");
	obs_property_list_add_string(summary_position, obs_module_text("MouseActivity.SummaryPosition.Bottom"),
				     "bottom");
	auto *distance_unit = obs_properties_add_list(behavior, "mouse_activity.distance_unit",
						      obs_module_text("MouseActivity.DistanceUnit"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(distance_unit, obs_module_text("MouseActivity.DistanceUnit.Pixels"), "pixels");
	obs_property_list_add_string(distance_unit, obs_module_text("MouseActivity.DistanceUnit.Metric"), "metric");
	obs_property_list_add_string(distance_unit, obs_module_text("MouseActivity.DistanceUnit.Imperial"), "imperial");
	obs_property_set_modified_callback(distance_unit, mouse_options_changed);
	obs_properties_add_int(behavior, "mouse_activity.mouse_dpi", obs_module_text("MouseActivity.MouseDPI"), 1,
			       100000, 1);
	obs_properties_add_bool(behavior, "mouse_activity.show_border", obs_module_text("MouseActivity.ShowBorder"));
	obs_properties_add_bool(behavior, "mouse_activity.show_center_mark",
				obs_module_text("MouseActivity.ShowCenterMark"));
	obs_properties_add_int_slider(behavior, "mouse_activity.trail_ms",
				      obs_module_text("MouseActivity.TrailDuration"), 100, 10000, 50);
	auto *gradient = obs_properties_add_list(appearance, "mouse_activity.heatmap_gradient",
						 obs_module_text("MouseActivity.HeatmapGradient"), OBS_COMBO_TYPE_LIST,
						 OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(gradient, obs_module_text("MouseActivity.HeatmapGradient.Spectrum"), "spectrum");
	obs_property_list_add_string(gradient, obs_module_text("MouseActivity.HeatmapGradient.Lime"), "lime");
	obs_property_list_add_string(gradient, obs_module_text("MouseActivity.HeatmapGradient.Ocean"), "ocean");
	obs_property_list_add_string(gradient, obs_module_text("MouseActivity.HeatmapGradient.MatchTheme"), "theme");
	obs_property_list_add_string(gradient, obs_module_text("MouseActivity.HeatmapGradient.Custom"), "custom");
	obs_property_set_modified_callback(gradient, mouse_options_changed);
	obs_properties_add_color_alpha(appearance, "mouse_activity.custom_gradient_low",
				       obs_module_text("MouseActivity.CustomGradientLow"));
	obs_properties_add_color_alpha(appearance, "mouse_activity.custom_gradient_middle",
				       obs_module_text("MouseActivity.CustomGradientMiddle"));
	obs_properties_add_color_alpha(appearance, "mouse_activity.custom_gradient_high",
				       obs_module_text("MouseActivity.CustomGradientHigh"));
	obs_properties_add_int_slider(appearance, "mouse_activity.hex_size", obs_module_text("MouseActivity.HexSize"),
				      2, 100, 1);
	obs_properties_add_int_slider(appearance, "mouse_activity.opacity",
				      obs_module_text("MouseActivity.HeatmapOpacity"), 0, 100, 1);
	auto *map = obs_properties_add_list(appearance, "mouse_activity.map", obs_module_text("MouseActivity.Map"),
					    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(map, obs_module_text("MouseActivity.Map.Movement"), "movement");
	obs_property_list_add_string(map, obs_module_text("MouseActivity.Map.Clicks"), "clicks");
	auto *actions = add_group(p, "input_activity.mouse_activity.actions", obs_module_text("Preferences.Actions"));
	obs_properties_add_path(actions, "mouse_activity.export_directory",
				obs_module_text("MouseActivity.ExportDirectory"), OBS_PATH_DIRECTORY, nullptr, nullptr);
	auto *export_format = obs_properties_add_list(actions, "mouse_activity.export_format",
						      obs_module_text("MouseActivity.ExportFormat"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(export_format, obs_module_text("MouseActivity.ExportFormat.PNG"), "png");
	obs_property_list_add_string(export_format, obs_module_text("MouseActivity.ExportFormat.SVG"), "svg");
	obs_properties_add_button2(
		actions, "mouse_activity.clear", obs_module_text("MouseActivity.ClearHeatmap"),
		[](obs_properties_t *, obs_property_t *, void *d) {
			static_cast<mouse_activity_source *>(d)->clear();
			return true;
		},
		data);
	if (data) {
		auto *settings = obs_source_get_settings(static_cast<mouse_activity_source *>(data)->source);
		mouse_options_changed(p, nullptr, settings);
		obs_data_release(settings);
	}
	return p;
}
obs_properties_t *mouse_properties(void *data)
{
	return mouse_properties_impl(data, true);
}

obs_properties_t *statistics_properties_impl(void *, bool include_common, const char *title_prefix = nullptr)
{
	auto *p = obs_properties_create();
	if (include_common)
		add_common_properties(p);
	auto *content = add_group(p, "input_activity.input_statistics.content", obs_module_text("Preferences.Content"));
	add_mode_title_properties(content, title_prefix);
	obs_properties_add_bool(content, "statistics.show_keys", obs_module_text("Statistics.ShowKeys"));
	obs_properties_add_bool(content, "statistics.show_clicks", obs_module_text("Statistics.ShowClicks"));
	obs_properties_add_bool(content, "statistics.show_actions", obs_module_text("Statistics.ShowActions"));
	auto *layout = add_group(p, "input_activity.input_statistics.layout", obs_module_text("Preferences.Layout"));
	auto *alignment = obs_properties_add_list(layout, "statistics.alignment",
						  obs_module_text("Statistics.Alignment"), OBS_COMBO_TYPE_LIST,
						  OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(alignment, obs_module_text("Statistics.Alignment.Left"), "left");
	obs_property_list_add_string(alignment, obs_module_text("Statistics.Alignment.Right"), "right");
	obs_properties_add_int_slider(layout, "statistics.horizontal_spacing",
				      obs_module_text("Statistics.HorizontalSpacing"), 0, 200, 1);
	obs_properties_add_int_slider(layout, "statistics.vertical_spacing",
				      obs_module_text("Statistics.VerticalSpacing"), 0, 200, 1);
	obs_properties_add_int_slider(layout, "statistics.group_spacing", obs_module_text("Statistics.GroupSpacing"), 0,
				      200, 1);
	auto *behavior =
		add_group(p, "input_activity.input_statistics.behavior", obs_module_text("Preferences.Behavior"));
	obs_properties_add_bool(behavior, "statistics.show_lap_keys", obs_module_text("Statistics.ShowLapKeys"));
	obs_properties_add_bool(behavior, "statistics.show_lap_clicks", obs_module_text("Statistics.ShowLapClicks"));
	obs_properties_add_bool(behavior, "statistics.show_lap_actions", obs_module_text("Statistics.ShowLapActions"));
	return p;
}
obs_properties_t *statistics_properties(void *data)
{
	return statistics_properties_impl(data, true);
}
