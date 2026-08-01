bool auto_detect_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	static_cast<dashboard_source *>(data)->auto_detect();
	return true;
}
bool reload_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	static_cast<dashboard_source *>(data)->reload();
	return true;
}
bool reset_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	static_cast<dashboard_source *>(data)->reset_statistics();
	return true;
}
bool advanced_positioning_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	obs_property_set_visible(obs_properties_get(props, "lol_dashboard.frame"),
				 obs_data_get_bool(settings, "lol_dashboard.advanced_positioning"));
	return true;
}
bool heatmap_gradient_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool custom = std::string(obs_data_get_string(settings, "lol_dashboard.heatmap_gradient")) == "custom";
	for (const char *name :
	     {"lol_dashboard.gradient_low", "lol_dashboard.gradient_middle", "lol_dashboard.gradient_high"})
		obs_property_set_visible(obs_properties_get(props, name), custom);
	return true;
}
bool camera_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool visible = obs_data_get_bool(settings, "lol_dashboard.show_camera");
	for (const char *name :
	     {"lol_dashboard.camera_source", "lol_dashboard.camera_width_percent",
	      "lol_dashboard.camera_height_percent", "lol_dashboard.camera_scale_percent",
	      "lol_dashboard.camera_translate_x_percent", "lol_dashboard.camera_translate_y_percent"})
		obs_property_set_visible(obs_properties_get(props, name), visible);
	return true;
}
bool minimap_cover_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool visible = obs_data_get_bool(settings, "lol_dashboard.show_minimap_cover");
	obs_property_set_visible(obs_properties_get(props, "lol_dashboard.minimap_cover_path"), visible);
	obs_property_set_visible(obs_properties_get(props, "lol_dashboard.minimap_cover_scale_percent"), visible);
	return true;
}

struct camera_option_context {
	obs_source_t *dashboard;
	obs_property_t *property;
};
bool add_camera_source(void *data, obs_source_t *source)
{
	auto *context = static_cast<camera_option_context *>(data);
	if (source != context->dashboard && obs_source_get_type(source) == OBS_SOURCE_TYPE_INPUT &&
	    (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO))
		obs_property_list_add_string(context->property, obs_source_get_name(source),
					     obs_source_get_uuid(source));
	return true;
}

obs_properties_t *properties(void *data)
{
	auto *properties = obs_properties_create();
	obs_properties_add_path(properties, path_key, obs_module_text("LeagueSafeArea.GameCfg"), OBS_PATH_FILE,
				"game.cfg", nullptr);
	obs_properties_add_button2(properties, "lol_dashboard.auto_detect",
				   obs_module_text("LeagueSafeArea.AutoDetect"), auto_detect_clicked, data);
	obs_properties_add_button2(properties, "lol_dashboard.reload", obs_module_text("LeagueSafeArea.Reload"),
				   reload_clicked, data);
	obs_properties_add_button2(properties, "lol_dashboard.reset",
				   obs_module_text("LoLPerformanceDashboard.ResetStatistics"), reset_clicked, data);
	auto *advanced = obs_properties_add_bool(properties, "lol_dashboard.advanced_positioning",
						 obs_module_text("LoLPerformanceDashboard.AdvancedPositioning"));
	obs_property_set_modified_callback(advanced, advanced_positioning_changed);
	auto *frame = obs_properties_add_group(properties, "lol_dashboard.frame",
					       obs_module_text("LoLPerformanceDashboard.GameFrame"), OBS_GROUP_NORMAL,
					       obs_properties_create());
	auto *frame_properties = obs_property_group_content(frame);
	obs_properties_add_int(frame_properties, "lol_dashboard.frame_left",
			       obs_module_text("LoLPerformanceDashboard.Left"), -32768, 32767, 1);
	obs_properties_add_int(frame_properties, "lol_dashboard.frame_top",
			       obs_module_text("LoLPerformanceDashboard.Top"), -32768, 32767, 1);
	obs_properties_add_int_slider(properties, "lol_dashboard.window",
				      obs_module_text("LoLPerformanceDashboard.Window"), 1, 60, 1);

	auto *camera = obs_properties_add_group(properties, "lol_dashboard.camera",
						obs_module_text("LoLPerformanceDashboard.Camera"), OBS_GROUP_NORMAL,
						obs_properties_create());
	auto *camera_properties = obs_property_group_content(camera);
	auto *show_camera = obs_properties_add_bool(camera_properties, "lol_dashboard.show_camera",
						    obs_module_text("LoLPerformanceDashboard.ShowCamera"));
	obs_property_set_modified_callback(show_camera, camera_changed);
	auto *camera_source = obs_properties_add_list(camera_properties, "lol_dashboard.camera_source",
						      obs_module_text("LoLPerformanceDashboard.CameraSource"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(camera_source, obs_module_text("LoLPerformanceDashboard.NoCamera"), "");
	if (data) {
		camera_option_context context{static_cast<dashboard_source *>(data)->source(), camera_source};
		obs_enum_sources(add_camera_source, &context);
	}
	obs_properties_add_int_slider(camera_properties, "lol_dashboard.camera_width_percent",
				      obs_module_text("LoLPerformanceDashboard.WidthPercent"), 1, 200, 1);
	obs_properties_add_int_slider(camera_properties, "lol_dashboard.camera_height_percent",
				      obs_module_text("LoLPerformanceDashboard.HeightPercent"), 1, 200, 1);
	obs_properties_add_int_slider(camera_properties, "lol_dashboard.camera_scale_percent",
				      obs_module_text("LoLPerformanceDashboard.ScalePercent"), 1, 400, 1);
	obs_properties_add_int_slider(camera_properties, "lol_dashboard.camera_translate_x_percent",
				      obs_module_text("LoLPerformanceDashboard.TranslateXPercent"), -200, 200, 1);
	obs_properties_add_int_slider(camera_properties, "lol_dashboard.camera_translate_y_percent",
				      obs_module_text("LoLPerformanceDashboard.TranslateYPercent"), -200, 200, 1);

	auto *cover = obs_properties_add_group(properties, "lol_dashboard.minimap_cover",
					       obs_module_text("LoLPerformanceDashboard.MinimapCover"),
					       OBS_GROUP_NORMAL, obs_properties_create());
	auto *cover_properties = obs_property_group_content(cover);
	auto *show_cover = obs_properties_add_bool(cover_properties, "lol_dashboard.show_minimap_cover",
						   obs_module_text("LoLPerformanceDashboard.ShowMinimapCover"));
	obs_property_set_modified_callback(show_cover, minimap_cover_changed);
	obs_properties_add_path(cover_properties, "lol_dashboard.minimap_cover_path",
				obs_module_text("LoLPerformanceDashboard.MinimapCoverImage"), OBS_PATH_FILE,
				"Image files (*.png *.jpg *.jpeg *.webp *.bmp)", nullptr);
	obs_properties_add_int_slider(cover_properties, "lol_dashboard.minimap_cover_scale_percent",
				      obs_module_text("LoLPerformanceDashboard.ScalePercent"), 1, 400, 1);

	auto *theme = obs_properties_add_group(properties, "lol_dashboard.theme",
					       obs_module_text("Preferences.Appearance"), OBS_GROUP_NORMAL,
					       obs_properties_create());
	auto *theme_properties = obs_property_group_content(theme);
	obs_properties_add_color_alpha(theme_properties, "activity.inactive_color",
				       obs_module_text("Activity.InactiveColor"));
	obs_properties_add_color_alpha(theme_properties, "activity.active_color",
				       obs_module_text("Activity.ActiveColor"));
	obs_properties_add_color_alpha(theme_properties, "activity.background_color",
				       obs_module_text("Activity.BackgroundColor"));
	auto *heatmap = obs_properties_add_group(properties, "lol_dashboard.heatmap",
						 obs_module_text("MouseActivity.HeatmapGradient"), OBS_GROUP_NORMAL,
						 obs_properties_create());
	auto *heatmap_properties = obs_property_group_content(heatmap);
	auto *gradient = obs_properties_add_list(heatmap_properties, "lol_dashboard.heatmap_gradient",
						 obs_module_text("MouseActivity.HeatmapGradient"), OBS_COMBO_TYPE_LIST,
						 OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(gradient, obs_module_text("MouseActivity.HeatmapGradient.Spectrum"), "spectrum");
	obs_property_list_add_string(gradient, obs_module_text("MouseActivity.HeatmapGradient.Lime"), "lime");
	obs_property_list_add_string(gradient, obs_module_text("MouseActivity.HeatmapGradient.Ocean"), "ocean");
	obs_property_list_add_string(gradient, obs_module_text("MouseActivity.HeatmapGradient.MatchTheme"), "theme");
	obs_property_list_add_string(gradient, obs_module_text("MouseActivity.HeatmapGradient.Custom"), "custom");
	obs_property_set_modified_callback(gradient, heatmap_gradient_changed);
	obs_properties_add_color_alpha(heatmap_properties, "lol_dashboard.gradient_low",
				       obs_module_text("MouseActivity.CustomGradientLow"));
	obs_properties_add_color_alpha(heatmap_properties, "lol_dashboard.gradient_middle",
				       obs_module_text("MouseActivity.CustomGradientMiddle"));
	obs_properties_add_color_alpha(heatmap_properties, "lol_dashboard.gradient_high",
				       obs_module_text("MouseActivity.CustomGradientHigh"));
	obs_properties_add_int_slider(heatmap_properties, "lol_dashboard.hex_size",
				      obs_module_text("MouseActivity.HexSize"), 2, 100, 1);
	if (data) {
		auto *settings = obs_source_get_settings(static_cast<dashboard_source *>(data)->source());
		heatmap_gradient_changed(heatmap_properties, nullptr, settings);
		camera_changed(camera_properties, nullptr, settings);
		minimap_cover_changed(cover_properties, nullptr, settings);
		obs_data_release(settings);
	}
	obs_property_set_visible(frame, data && static_cast<dashboard_source *>(data)->uses_advanced_positioning());
	return properties;
}
