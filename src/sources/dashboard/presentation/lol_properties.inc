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
bool auto_link_captures_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	static_cast<dashboard_source *>(data)->auto_link_captures();
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
bool heatmap_radius_changed(obs_properties_t *, obs_property_t *, obs_data_t *settings)
{
	lol_heatmap::set_radius_percent(obs_data_get_double(settings, "lol_dashboard.hex_radius_percent"));
	return true;
}
bool camera_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool visible = obs_data_get_bool(settings, "lol_dashboard.show_camera");
	for (const char *name : {"lol_dashboard.camera_source", "lol_dashboard.camera_width_percent",
				 "lol_dashboard.camera_height_percent", "lol_dashboard.camera_scale_percent",
				 "lol_dashboard.camera_translate_x_percent", "lol_dashboard.camera_translate_y_percent",
				 "lol_dashboard.camera_background_color"})
		obs_property_set_visible(obs_properties_get(props, name), visible);
	return true;
}
bool minimap_cover_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool visible = obs_data_get_bool(settings, "lol_dashboard.show_minimap_cover");
	const bool custom_image = visible && obs_data_get_bool(settings, "lol_dashboard.use_custom_minimap_cover");
	for (const char *name :
	     {"lol_dashboard.use_custom_minimap_cover", "lol_dashboard.minimap_cover_width_percent",
	      "lol_dashboard.minimap_cover_height_percent", "lol_dashboard.minimap_cover_scale_percent",
	      "lol_dashboard.minimap_cover_translate_x_percent", "lol_dashboard.minimap_cover_translate_y_percent"})
		obs_property_set_visible(obs_properties_get(props, name), visible);
	for (const char *name :
	     {"lol_dashboard.minimap_cover_path", "lol_dashboard.minimap_cover_alpha_padding_percent"})
		obs_property_set_visible(obs_properties_get(props, name), custom_image);
	return true;
}
bool typography_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	for (const char *role : {"numbers", "numbers_secondary", "number_labels", "button_labels"}) {
		const std::string prefix = std::string("lol_dashboard.typography.") + role;
		const bool science_gothic = std::string(obs_data_get_string(settings, (prefix + ".family").c_str())) ==
					    "Science Gothic";
		for (const char *axis : {"width", "slant"}) {
			auto *property = obs_properties_get(props, (prefix + "." + axis).c_str());
			if (property)
				obs_property_set_visible(property, science_gothic);
		}
	}
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

void defaults(obs_data_t *settings)
{
	lol_report_manager::defaults(settings);
	obs_data_set_default_bool(settings, "lol_dashboard.advanced_positioning", false);
	obs_data_set_default_bool(settings, "lol_dashboard.always_visible", false);
	obs_data_set_default_bool(settings, "lol_dashboard.reset_at_game_start", true);
	obs_data_set_default_string(settings, league_capture_switcher::game_source_key, "");
	obs_data_set_default_string(settings, league_capture_switcher::client_source_key, "");
	obs_data_set_default_int(settings, "lol_dashboard.window", 60);
	obs_data_set_default_bool(settings, "lol_dashboard.show_intensity", true);
	obs_data_set_default_bool(settings, "lol_dashboard.show_keys", true);
	obs_data_set_default_bool(settings, "lol_dashboard.show_mouse_activity", true);
	obs_data_set_default_bool(settings, "lol_dashboard.show_camera", false);
	obs_data_set_default_string(settings, "lol_dashboard.camera_source", "");
	obs_data_set_default_int(settings, "lol_dashboard.camera_width_percent", 133);
	obs_data_set_default_int(settings, "lol_dashboard.camera_height_percent", 100);
	obs_data_set_default_int(settings, "lol_dashboard.camera_scale_percent", 100);
	obs_data_set_default_int(settings, "lol_dashboard.camera_translate_x_percent", 0);
	obs_data_set_default_int(settings, "lol_dashboard.camera_translate_y_percent", 0);
	obs_data_set_default_int(settings, "lol_dashboard.camera_background_color", 0xff1a1a1a);
	obs_data_set_default_bool(settings, "lol_dashboard.show_minimap_cover", true);
	obs_data_set_default_bool(settings, "lol_dashboard.use_custom_minimap_cover", false);
	obs_data_set_default_string(settings, "lol_dashboard.minimap_cover_path", "");
	obs_data_set_default_int(settings, "lol_dashboard.minimap_cover_alpha_padding_percent", 0);
	obs_data_set_default_int(settings, "lol_dashboard.minimap_cover_width_percent", 100);
	obs_data_set_default_int(settings, "lol_dashboard.minimap_cover_height_percent", 100);
	obs_data_set_default_int(settings, "lol_dashboard.minimap_cover_scale_percent", 100);
	obs_data_set_default_int(settings, "lol_dashboard.minimap_cover_translate_x_percent", 0);
	obs_data_set_default_int(settings, "lol_dashboard.minimap_cover_translate_y_percent", 0);
	obs_data_set_default_int(settings, "activity.inactive_color", 0xff425e62);
	obs_data_set_default_int(settings, "activity.active_color", 0xff83c1dd);
	obs_data_set_default_int(settings, "activity.background_color", 0x00000000);
	obs_data_set_default_string(settings, "lol_dashboard.heatmap_gradient", "spectrum");
	obs_data_set_default_int(settings, "lol_dashboard.gradient_low", 0xffeb6325);
	obs_data_set_default_int(settings, "lol_dashboard.gradient_middle", 0xff15ccfa);
	obs_data_set_default_int(settings, "lol_dashboard.gradient_high", 0xff4444ef);
	obs_data_set_default_int(settings, "lol_dashboard.hex_size", 10);
	obs_data_set_default_double(settings, "lol_dashboard.hex_radius_percent", lol_heatmap::default_radius_percent);
	obs_data_set_default_int(settings, "lol_dashboard.section_padding", 20);
	obs_data_set_default_int(settings, "lol_dashboard.element_padding", 20);
	obs_data_set_default_int(settings, "lol_dashboard.element_x_gap", 10);
	obs_data_set_default_int(settings, "lol_dashboard.element_y_gap", 10);
	obs_data_set_default_int(settings, "lol_dashboard.within_element_gap", 10);
	obs_data_set_default_int(settings, "lol_dashboard.label_spacing", 10);
	obs_data_set_default_int(settings, "lol_dashboard.intensity_padding", 120);
	for (const auto &[role, family, size] :
	     std::initializer_list<std::tuple<const char *, const char *, int>>{{"numbers", "Inter", 30},
										{"numbers_secondary", "Inter", 18},
										{"number_labels", "Inter", 18},
										{"button_labels", "Inter", 30}}) {
		const std::string prefix = std::string("lol_dashboard.typography.") + role;
		obs_data_set_default_string(settings, (prefix + ".family").c_str(), family);
		obs_data_set_default_double(settings, (prefix + ".optical_size").c_str(), 22.0);
		obs_data_set_default_double(settings, (prefix + ".weight").c_str(), 700.0);
		obs_data_set_default_double(settings, (prefix + ".width").c_str(), 100.0);
		obs_data_set_default_double(settings, (prefix + ".slant").c_str(), 0.0);
		obs_data_set_default_int(settings, (prefix + ".size").c_str(), size);
		obs_data_set_default_bool(settings, (prefix + ".all_caps").c_str(), false);
	}
}

obs_properties_t *properties(void *data)
{
	auto *properties = obs_properties_create();
	if (data)
		static_cast<dashboard_source *>(data)->report_manager().add_properties(properties);
	obs_properties_add_path(properties, path_key, obs_module_text("LeagueSafeArea.GameCfg"), OBS_PATH_FILE,
				"game.cfg", nullptr);
	obs_properties_add_button2(properties, "lol_dashboard.auto_detect",
				   obs_module_text("LeagueSafeArea.AutoDetect"), auto_detect_clicked, data);
	obs_properties_add_button2(properties, "lol_dashboard.reload", obs_module_text("LeagueSafeArea.Reload"),
				   reload_clicked, data);
	obs_properties_add_button2(properties, "lol_dashboard.reset",
				   obs_module_text("LoLPerformanceDashboard.ResetStatistics"), reset_clicked, data);
	obs_properties_add_bool(properties, "lol_dashboard.reset_at_game_start",
				obs_module_text("LoLPerformanceDashboard.ResetAtGameStart"));
	obs_properties_add_bool(properties, "lol_dashboard.always_visible",
				obs_module_text("LoLPerformanceDashboard.DebugMode"));
	auto *capture_switching = obs_properties_add_group(properties, "lol_dashboard.capture_switching",
							   obs_module_text("LoLPerformanceDashboard.CaptureSwitching"),
							   OBS_GROUP_NORMAL, obs_properties_create());
	auto *capture_properties = obs_property_group_content(capture_switching);
	obs_properties_add_button2(capture_properties, "lol_dashboard.auto_link_captures",
				   obs_module_text("LoLPerformanceDashboard.AutoLinkCaptures"),
				   auto_link_captures_clicked, data);
	auto *game_capture = obs_properties_add_list(capture_properties, league_capture_switcher::game_source_key,
						     obs_module_text("LoLPerformanceDashboard.GameCapture"),
						     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	auto *client_capture = obs_properties_add_list(capture_properties, league_capture_switcher::client_source_key,
						       obs_module_text("LoLPerformanceDashboard.ClientCapture"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (obs_property_t *property : {game_capture, client_capture})
		obs_property_list_add_string(property, obs_module_text("LoLPerformanceDashboard.NoCapture"), "");
	for (const auto &[name, uuid] : league_capture_switcher::capture_sources()) {
		obs_property_list_add_string(game_capture, name.c_str(), uuid.c_str());
		obs_property_list_add_string(client_capture, name.c_str(), uuid.c_str());
	}
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
	auto *regions = obs_properties_add_group(properties, "lol_dashboard.regions",
						 obs_module_text("LoLPerformanceDashboard.Regions"), OBS_GROUP_NORMAL,
						 obs_properties_create());
	auto *region_properties = obs_property_group_content(regions);
	obs_properties_add_bool(region_properties, "lol_dashboard.show_intensity",
				obs_module_text("LoLPerformanceDashboard.ShowIntensity"));
	obs_properties_add_bool(region_properties, "lol_dashboard.show_keys",
				obs_module_text("LoLPerformanceDashboard.ShowKeys"));
	obs_properties_add_bool(region_properties, "lol_dashboard.show_mouse_activity",
				obs_module_text("LoLPerformanceDashboard.ShowMouseActivity"));

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
	obs_properties_add_color_alpha(camera_properties, "lol_dashboard.camera_background_color",
				       obs_module_text("LoLPerformanceDashboard.CameraBackgroundColor"));

	auto *cover = obs_properties_add_group(properties, "lol_dashboard.minimap_cover",
					       obs_module_text("LoLPerformanceDashboard.MinimapCover"),
					       OBS_GROUP_NORMAL, obs_properties_create());
	auto *cover_properties = obs_property_group_content(cover);
	auto *show_cover = obs_properties_add_bool(cover_properties, "lol_dashboard.show_minimap_cover",
						   obs_module_text("LoLPerformanceDashboard.ShowMinimapCover"));
	obs_property_set_modified_callback(show_cover, minimap_cover_changed);
	auto *use_custom_cover =
		obs_properties_add_bool(cover_properties, "lol_dashboard.use_custom_minimap_cover",
					obs_module_text("LoLPerformanceDashboard.UseCustomMinimapCover"));
	obs_property_set_modified_callback(use_custom_cover, minimap_cover_changed);
	obs_properties_add_path(cover_properties, "lol_dashboard.minimap_cover_path",
				obs_module_text("LoLPerformanceDashboard.MinimapCoverImage"), OBS_PATH_FILE,
				"Image files (*.png *.jpg *.jpeg *.webp *.bmp)", nullptr);
	obs_properties_add_int_slider(cover_properties, "lol_dashboard.minimap_cover_alpha_padding_percent",
				      obs_module_text("LoLPerformanceDashboard.MinimapCoverAlphaPadding"), 0, 50, 1);
	obs_properties_add_int_slider(cover_properties, "lol_dashboard.minimap_cover_width_percent",
				      obs_module_text("LoLPerformanceDashboard.WidthPercent"), 1, 200, 1);
	obs_properties_add_int_slider(cover_properties, "lol_dashboard.minimap_cover_height_percent",
				      obs_module_text("LoLPerformanceDashboard.HeightPercent"), 1, 200, 1);
	obs_properties_add_int_slider(cover_properties, "lol_dashboard.minimap_cover_scale_percent",
				      obs_module_text("LoLPerformanceDashboard.ScalePercent"), 1, 400, 1);
	obs_properties_add_int_slider(cover_properties, "lol_dashboard.minimap_cover_translate_x_percent",
				      obs_module_text("LoLPerformanceDashboard.TranslateXPercent"), -200, 200, 1);
	obs_properties_add_int_slider(cover_properties, "lol_dashboard.minimap_cover_translate_y_percent",
				      obs_module_text("LoLPerformanceDashboard.TranslateYPercent"), -200, 200, 1);

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
	auto *spacing = obs_properties_add_group(properties, "lol_dashboard.spacing",
						 obs_module_text("LoLPerformanceDashboard.Spacing"), OBS_GROUP_NORMAL,
						 obs_properties_create());
	auto *spacing_properties = obs_property_group_content(spacing);
	for (const auto &[key, label, description, maximum] :
	     std::initializer_list<std::tuple<const char *, const char *, const char *, int>>{
		     {"lol_dashboard.section_padding", "LoLPerformanceDashboard.SectionPadding",
		      "LoLPerformanceDashboard.SectionPadding.Tooltip", 100},
		     {"lol_dashboard.element_padding", "LoLPerformanceDashboard.ElementPadding",
		      "LoLPerformanceDashboard.ElementPadding.Tooltip", 100},
		     {"lol_dashboard.element_x_gap", "LoLPerformanceDashboard.ElementXGap",
		      "LoLPerformanceDashboard.ElementXGap.Tooltip", 100},
		     {"lol_dashboard.element_y_gap", "LoLPerformanceDashboard.ElementYGap",
		      "LoLPerformanceDashboard.ElementYGap.Tooltip", 100},
		     {"lol_dashboard.within_element_gap", "LoLPerformanceDashboard.WithinElementGap",
		      "LoLPerformanceDashboard.WithinElementGap.Tooltip", 100},
		     {"lol_dashboard.label_spacing", "LoLPerformanceDashboard.LabelSpacing",
		      "LoLPerformanceDashboard.LabelSpacing.Tooltip", 100},
		     {"lol_dashboard.intensity_padding", "LoLPerformanceDashboard.IntensityPadding",
		      "LoLPerformanceDashboard.IntensityPadding.Tooltip", 400}}) {
		auto *property =
			obs_properties_add_int_slider(spacing_properties, key, obs_module_text(label), 0, maximum, 1);
		obs_property_set_long_description(property, obs_module_text(description));
	}
	auto *typography = obs_properties_add_group(properties, "lol_dashboard.typography",
						    obs_module_text("LoLPerformanceDashboard.Typography"),
						    OBS_GROUP_NORMAL, obs_properties_create());
	auto *typography_properties = obs_property_group_content(typography);
	for (const auto &[role, title] : std::initializer_list<std::tuple<const char *, const char *>>{
		     {"numbers", "LoLPerformanceDashboard.NumberPrimary"},
		     {"numbers_secondary", "LoLPerformanceDashboard.NumbersSecondary"},
		     {"number_labels", "LoLPerformanceDashboard.NumberLabels"},
		     {"button_labels", "LoLPerformanceDashboard.LiveKeysLabels"}}) {
		const std::string prefix = std::string("lol_dashboard.typography.") + role;
		auto *group = obs_properties_add_group(typography_properties, prefix.c_str(), obs_module_text(title),
						       OBS_GROUP_NORMAL, obs_properties_create());
		auto *group_properties = obs_property_group_content(group);
		auto *family = obs_properties_add_list(group_properties, (prefix + ".family").c_str(),
						       obs_module_text("LoLPerformanceDashboard.FontFamily"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_list_add_string(family, "Science Gothic", "Science Gothic");
		obs_property_list_add_string(family, "Inter", "Inter");
		obs_property_set_modified_callback(family, typography_changed);
		for (const auto &[axis, label, minimum, maximum] :
		     std::initializer_list<std::tuple<const char *, const char *, double, double>>{
			     {"optical_size", "LoLPerformanceDashboard.FontOpticalSize", 8, 144},
			     {"weight", "LoLPerformanceDashboard.FontWeight", 100, 900},
			     {"width", "LoLPerformanceDashboard.FontWidth", 50, 200},
			     {"slant", "LoLPerformanceDashboard.FontSlant", -20, 20}})
			obs_properties_add_float_slider(group_properties, (prefix + "." + axis).c_str(),
							obs_module_text(label), minimum, maximum, 1.0);
		obs_properties_add_bool(group_properties, (prefix + ".all_caps").c_str(),
					obs_module_text("LoLPerformanceDashboard.AllCaps"));
		obs_properties_add_int_slider(group_properties, (prefix + ".size").c_str(),
					      obs_module_text("LoLPerformanceDashboard.FontSize"), 8, 100, 1);
	}
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
	auto *radius = obs_properties_add_float_slider(heatmap_properties, "lol_dashboard.hex_radius_percent",
						       obs_module_text("MouseActivity.HexSize"), 0.1, 20.0, 0.1);
	obs_property_set_modified_callback(radius, heatmap_radius_changed);
	if (data) {
		auto *settings = obs_source_get_settings(static_cast<dashboard_source *>(data)->source());
		obs_data_set_double(settings, "lol_dashboard.hex_radius_percent", lol_heatmap::radius_percent());
		heatmap_gradient_changed(heatmap_properties, nullptr, settings);
		camera_changed(camera_properties, nullptr, settings);
		minimap_cover_changed(cover_properties, nullptr, settings);
		typography_changed(typography_properties, nullptr, settings);
		obs_data_release(settings);
	}
	obs_property_set_visible(frame, data && static_cast<dashboard_source *>(data)->uses_advanced_positioning());
	return properties;
}
