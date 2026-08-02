
void add_common_properties(obs_properties_t *props, bool allow_height = true, bool include_live_keys_layout = false)
{
	auto *target = add_group(props, "input_activity.general.target", obs_module_text("Preferences.InputTarget"));
	auto *target_type = obs_properties_add_list(target, "activity.target.type", obs_module_text("Activity.Target"),
						    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(target_type, obs_module_text("Activity.Target.All"), "all");
	obs_property_list_add_string(target_type, obs_module_text("Activity.Target.Display"), "display");
	obs_property_list_add_string(target_type, obs_module_text("Activity.Target.Application"), "application");
	obs_property_list_add_string(target_type, obs_module_text("Activity.Target.Window"), "window");
	obs_property_set_modified_callback(target_type, target_type_changed);
	auto *target_display = obs_properties_add_list(target, "activity.target.display",
						       obs_module_text("Activity.Target.Display"), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_INT);
	for (const auto &display : uiohook::target_displays())
		obs_property_list_add_int(target_display, display.label.c_str(), display.id);
	obs_property_set_modified_callback(target_display, target_type_changed);
	auto *target_application = obs_properties_add_list(target, "activity.target.application",
							   obs_module_text("Activity.Target.Application"),
							   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const auto &application : uiohook::target_applications())
		obs_property_list_add_string(target_application, application.label.c_str(), application.id.c_str());
	auto *target_window = obs_properties_add_list(target, "activity.target.window",
						      obs_module_text("Activity.Target.Window"), OBS_COMBO_TYPE_LIST,
						      OBS_COMBO_FORMAT_STRING);
	for (const auto &window : uiohook::target_windows()) {
		const std::string id = window.application_id + "#" + std::to_string(window.id);
		obs_property_list_add_string(target_window, window.label.c_str(), id.c_str());
	}
	obs_property_set_visible(target_display, false);
	obs_property_set_visible(target_application, false);
	obs_property_set_visible(target_window, false);
	auto *target_rectangle = obs_properties_add_bool(target, "activity.target.display_rectangle",
							 obs_module_text("Activity.Target.Rectangle"));
	obs_property_set_modified_callback(target_rectangle, target_type_changed);
	obs_properties_add_int(target, "activity.target.rectangle.left", obs_module_text("MouseActivity.Region.Left"),
			       -32768, 32767, 1);
	obs_properties_add_int(target, "activity.target.rectangle.top", obs_module_text("MouseActivity.Region.Top"),
			       -32768, 32767, 1);
	obs_properties_add_int(target, "activity.target.rectangle.right", obs_module_text("MouseActivity.Region.Right"),
			       -32768, 32767, 1);
	obs_properties_add_int(target, "activity.target.rectangle.bottom",
			       obs_module_text("MouseActivity.Region.Bottom"), -32768, 32767, 1);
	auto *layout = add_group(props, "input_activity.general.layout", obs_module_text("Preferences.LayoutSizing"));
	obs_properties_add_int(layout, "activity.width", obs_module_text("Activity.Width"), 64, 3840, 1);
	if (allow_height)
		obs_properties_add_int(layout, "activity.height", obs_module_text("Activity.Height"), 32, 2160, 1);
	obs_properties_add_int(layout, "activity.padding", obs_module_text("Activity.Padding"), 0, 200, 1);
	obs_properties_add_int_slider(layout, "activity.within_spacing", obs_module_text("Activity.WithinSpacing"), 0,
				      200, 1);
	obs_properties_add_int_slider(layout, "activity.group_spacing", obs_module_text("Activity.GroupSpacing"), 0,
				      200, 1);
	auto *alignment = obs_properties_add_list(layout, "activity.alignment", obs_module_text("Activity.Alignment"),
						  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(alignment, obs_module_text("Activity.Alignment.Left"), "left");
	obs_property_list_add_string(alignment, obs_module_text("Activity.Alignment.Right"), "right");
	if (include_live_keys_layout)
		obs_properties_add_bool(layout, "live_keys.invert_layout", obs_module_text("LiveKeys.InvertLayout"));
	auto *typography =
		add_group(props, "input_activity.general.typography", obs_module_text("Preferences.Typography"));
	obs_properties_add_font(typography, "activity.font", obs_module_text("Activity.Font"));
	obs_properties_add_int(typography, "activity.section_title_size", obs_module_text("Activity.TitleSize"), 8, 256,
			       1);
	obs_properties_add_int(typography, "activity.input_title_size", obs_module_text("Activity.SubtitleSize"), 8,
			       256, 1);
	obs_properties_add_int(typography, "activity.input_value_size", obs_module_text("Activity.TextSize"), 8, 256,
			       1);
	auto *appearance =
		add_group(props, "input_activity.general.appearance", obs_module_text("Preferences.Appearance"));
	obs_properties_add_color_alpha(appearance, "activity.inactive_color",
				       obs_module_text("Activity.InactiveColor"));
	obs_properties_add_color_alpha(appearance, "activity.active_color", obs_module_text("Activity.ActiveColor"));
	obs_properties_add_color_alpha(appearance, "activity.background_color",
				       obs_module_text("Activity.BackgroundColor"));
}

void add_mode_title_properties(obs_properties_t *props, const char *prefix = nullptr)
{
	const std::string setting_prefix = prefix ? std::string(prefix) + "." : "activity.";
	obs_properties_add_bool(props, (setting_prefix + "show_title").c_str(), obs_module_text("Activity.ShowTitle"));
	obs_properties_add_text(props, (setting_prefix + "title").c_str(), obs_module_text("Activity.Title"),
				OBS_TEXT_DEFAULT);
}
template<typename T> void register_source(const char *id, obs_properties_t *(*properties)(void *))
{
	obs_source_info info{};
	info.id = id;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO;
	if constexpr (std::is_same_v<T, live_keys_source>)
		info.get_name = [](void *) {
			return obs_module_text("LiveKeys");
		};
	else if constexpr (std::is_same_v<T, mouse_activity_source>)
		info.get_name = [](void *) {
			return obs_module_text("MouseActivity");
		};
	else if constexpr (std::is_same_v<T, input_intensity_source>)
		info.get_name = [](void *) {
			return obs_module_text("InputIntensity");
		};
	else if constexpr (std::is_same_v<T, unified_source>)
		info.get_name = [](void *) {
			return obs_module_text("InputActivity");
		};
	else
		info.get_name = [](void *) {
			return obs_module_text("InputStatistics");
		};
	info.create = [](obs_data_t *settings, obs_source_t *source) {
		return static_cast<void *>(new T(source, settings));
	};
	info.destroy = [](void *data) {
		delete static_cast<T *>(data);
	};
	info.update = [](void *data, obs_data_t *settings) {
		static_cast<T *>(data)->update(settings);
	};
	info.video_tick = [](void *data, float seconds) {
		static_cast<T *>(data)->tick(seconds);
	};
	info.video_render = [](void *data, gs_effect_t *effect) {
		static_cast<T *>(data)->draw(effect);
	};
	info.get_width = [](void *data) {
		if constexpr (std::is_same_v<T, unified_source>)
			return static_cast<T *>(data)->width();
		else
			return static_cast<uint32_t>(static_cast<T *>(data)->width);
	};
	info.get_height = [](void *data) {
		if constexpr (std::is_same_v<T, unified_source>)
			return static_cast<T *>(data)->height();
		else
			return static_cast<uint32_t>(static_cast<T *>(data)->height);
	};
	info.get_properties = properties;
	info.get_defaults = [](obs_data_t *settings) {
		obs_data_set_default_int(settings, "activity.width", 640);
		obs_data_set_default_int(settings, "activity.height", 240);
		obs_data_set_default_int(settings, "activity.padding", 20);
		obs_data_set_default_int(settings, "activity.font_size", default_text_font_size);
		obs_data_set_default_int(settings, "activity.section_title_size", default_title_font_size);
		obs_data_set_default_int(settings, "activity.input_title_size", default_subtitle_font_size);
		obs_data_set_default_int(settings, "activity.input_value_size", default_text_font_size);
		// OBS stores color channels as AABBGGRR; the user-facing ARGB defaults are
		// ff625e42 (inactive) and ffddc183 (active).
		obs_data_set_default_int(settings, "activity.inactive_color", 0xff425e62);
		obs_data_set_default_int(settings, "activity.active_color", 0xff83c1dd);
		obs_data_set_default_int(settings, "activity.within_spacing", 10);
		obs_data_set_default_int(settings, "activity.group_spacing", 20);
		obs_data_set_default_string(settings, "activity.alignment", "left");
		obs_data_set_default_int(settings, "activity.text_color", 0xffffffff);
		obs_data_set_default_int(settings, "activity.background_color", 0x00000000);
		obs_data_set_default_bool(settings, "activity.text_shadow", false);
		obs_data_set_default_int(settings, "activity.text_shadow_color", 0xcc000000);
		obs_data_set_default_int(settings, "activity.text_shadow_offset", 2);
		obs_data_set_default_bool(settings, "activity.show_title", true);
		obs_data_set_default_string(settings, "activity.target.type", "all");
		obs_data_set_default_bool(settings, "activity.target.display_rectangle", false);
		obs_data_set_default_int(settings, "activity.target.rectangle.left", 0);
		obs_data_set_default_int(settings, "activity.target.rectangle.top", 0);
		obs_data_set_default_int(settings, "activity.target.rectangle.right", 1919);
		obs_data_set_default_int(settings, "activity.target.rectangle.bottom", 1079);
		if constexpr (std::is_same_v<T, unified_source>) {
			obs_data_set_default_int(settings, "activity.width", 240);
			obs_data_set_default_int(settings, "activity.height", 640);
			obs_data_set_default_string(settings, "input_activity.mode", "live_keys");
			obs_data_set_default_bool(settings, "input_activity.show_advanced_settings", false);
			obs_data_set_default_string(settings, "activity.title", "Live Keys");
			obs_data_set_default_bool(settings, "activity.show_title", false);
			obs_data_set_default_bool(settings, "mouse_activity.show_title", true);
			obs_data_set_default_string(settings, "mouse_activity.title", "Mouse Activity");
			obs_data_set_default_bool(settings, "input_intensity.show_title", true);
			obs_data_set_default_string(settings, "input_intensity.title", "Input Intensity");
			obs_data_set_default_bool(settings, "statistics.show_title", true);
			obs_data_set_default_string(settings, "statistics.title", "Input Statistics");
			obs_data_set_default_int(settings, "live_keys.maximum", 4);
			obs_data_set_default_int(settings, "live_keys.top_n", 8);
			obs_data_set_default_int(settings, "live_keys.row_height", 96);
			obs_data_set_default_string(settings, "live_keys.top_n_alignment", "left");
			obs_data_set_default_bool(settings, "live_keys.invert_layout", false);
			obs_data_set_default_int(settings, "live_keys.horizontal_spacing", 10);
			obs_data_set_default_int(settings, "live_keys.vertical_spacing", 10);
			obs_data_set_default_string(settings, "live_keys.total_position", "above");
			obs_data_set_default_int(settings, "live_keys.most_used_element_spacing", 10);
			obs_data_set_default_int(settings, "live_keys.group_spacing", 10);
			obs_data_set_default_bool(settings, "live_keys.show_most_used", true);
			obs_data_set_default_string(settings, "live_keys.layout_order", "live_first");
			obs_data_set_default_bool(settings, "live_keys.show_live_title", true);
			obs_data_set_default_string(settings, "live_keys.live_title", "Live Keys");
			obs_data_set_default_bool(settings, "live_keys.show_most_used_title", true);
			obs_data_set_default_string(settings, "live_keys.most_used_title", "Most Used Keys");
			obs_data_set_default_int(settings, "live_keys.fade_ms", 2000);
			obs_data_set_default_string(settings, "live_keys.fade_curve", "ease_out");
			obs_data_set_default_string(settings, "live_keys.blacklist", "");
			obs_data_set_default_int(settings, "live_keys.color", 0xffeb6325);
			obs_data_set_default_int(settings, "live_keys.pressed_color", 0xff4444ef);
			obs_data_set_default_string(settings, "mouse_activity.left_label", "L");
			obs_data_set_default_string(settings, "mouse_activity.right_label", "R");
			obs_data_set_default_string(settings, "mouse_activity.middle_label", "M");
			obs_data_set_default_int(settings, "mouse_activity.trail_ms", 1500);
			obs_data_set_default_string(settings, "mouse_activity.heatmap_gradient", "spectrum");
			obs_data_set_default_int(settings, "mouse_activity.custom_gradient_low", 0xffeb6325);
			obs_data_set_default_int(settings, "mouse_activity.custom_gradient_middle", 0xff15ccfa);
			obs_data_set_default_int(settings, "mouse_activity.custom_gradient_high", 0xff4444ef);
			obs_data_set_default_int(settings, "mouse_activity.hex_size", 8);
			obs_data_set_default_int(settings, "mouse_activity.opacity", 100);
			obs_data_set_default_string(settings, "mouse_activity.map", "movement");
			obs_data_set_default_bool(settings, "mouse_activity.show_heatmap", true);
			obs_data_set_default_bool(settings, "mouse_activity.show_live_mouse", true);
			obs_data_set_default_bool(settings, "mouse_activity.show_distance", true);
			obs_data_set_default_bool(settings, "mouse_activity.show_clicks", true);
			obs_data_set_default_string(settings, "mouse_activity.summary_position", "top");
			obs_data_set_default_string(settings, "mouse_activity.distance_unit", "metric");
			obs_data_set_default_int(settings, "mouse_activity.mouse_dpi", 2800);
			obs_data_set_default_int(settings, "mouse_activity.color", 0xffeb6325);
			obs_data_set_default_int(settings, "mouse_activity.left_color", 0xffeb6325);
			obs_data_set_default_int(settings, "mouse_activity.right_color", 0xff4444ef);
			obs_data_set_default_int(settings, "mouse_activity.middle_color", 0xff15ccfa);
			obs_data_set_default_string(settings, "mouse_activity.region", "display");
			obs_data_set_default_int(settings, "mouse_activity.region.left", 0);
			obs_data_set_default_int(settings, "mouse_activity.region.top", 0);
			obs_data_set_default_int(settings, "mouse_activity.region.right", 1919);
			obs_data_set_default_int(settings, "mouse_activity.region.bottom", 1079);
			obs_data_set_default_int(settings, "statistics.horizontal_spacing", 10);
			obs_data_set_default_int(settings, "statistics.vertical_spacing", 10);
			obs_data_set_default_int(settings, "statistics.group_spacing", 10);
			obs_data_set_default_string(settings, "statistics.alignment", "left");
			obs_data_set_default_bool(settings, "statistics.show_keys", true);
			obs_data_set_default_bool(settings, "statistics.show_clicks", true);
			obs_data_set_default_bool(settings, "statistics.show_actions", true);
			obs_data_set_default_int(settings, "statistics.theme_color", 0xffeb6325);
			obs_data_set_default_int(settings, "statistics.pressed_color", 0xff4444ef);
			obs_data_set_default_int(settings, "input_intensity.window", 30);
			obs_data_set_default_int(settings, "input_intensity.metric_count", 2);
			obs_data_set_default_int(settings, "input_intensity.element_spacing", 10);
			obs_data_set_default_string(settings, "input_intensity.layout", "column");
			obs_data_set_default_string(settings, "input_intensity.velocity_unit", "metric");
			obs_data_set_default_int(settings, "input_intensity.mouse_dpi", 2800);
			obs_data_set_default_int(settings, "input_intensity.theme_color", 0xffeb6325);
			obs_data_set_default_int(settings, "input_intensity.pressed_color", 0xff4444ef);
			for (size_t index = 0; index < intensity_max_fields; ++index) {
				const std::string prefix = "input_intensity.row" + std::to_string(index) + ".";
				obs_data_set_default_bool(settings, (prefix + "enabled").c_str(), index < 2);
				obs_data_set_default_string(settings, (prefix + "metric").c_str(),
							    index == 1 ? "velocity" : "actions");
				obs_data_set_default_int(settings, (prefix + "key").c_str(), VC_SPACE);
				obs_data_set_default_int(settings, (prefix + "button").c_str(), MOUSE_BUTTON1);
				obs_data_set_default_string(settings, (prefix + "title").c_str(), "");
				obs_data_set_default_string(settings, (prefix + "key_scope").c_str(), "all");
				obs_data_set_default_string(settings, (prefix + "key_list").c_str(), "");
			}
		}
		if constexpr (std::is_same_v<T, live_keys_source>) {
			obs_data_set_default_int(settings, "activity.width", 240);
			obs_data_set_default_int(settings, "activity.height", 640);
			obs_data_set_default_string(settings, "activity.title", "Live Keys");
			obs_data_set_default_bool(settings, "activity.show_title", false);
			obs_data_set_default_int(settings, "live_keys.maximum", 4);
			obs_data_set_default_int(settings, "live_keys.top_n", 8);
			obs_data_set_default_int(settings, "live_keys.row_height", 96);
			obs_data_set_default_string(settings, "live_keys.top_n_alignment", "left");
			obs_data_set_default_bool(settings, "live_keys.invert_layout", false);
			obs_data_set_default_int(settings, "live_keys.horizontal_spacing", 10);
			obs_data_set_default_int(settings, "live_keys.vertical_spacing", 10);
			obs_data_set_default_string(settings, "live_keys.total_position", "above");
			obs_data_set_default_int(settings, "live_keys.most_used_element_spacing", 10);
			obs_data_set_default_int(settings, "live_keys.group_spacing", 10);
			obs_data_set_default_bool(settings, "live_keys.show_most_used", true);
			obs_data_set_default_string(settings, "live_keys.layout_order", "live_first");
			obs_data_set_default_bool(settings, "live_keys.show_live_title", true);
			obs_data_set_default_string(settings, "live_keys.live_title", "Live Keys");
			obs_data_set_default_bool(settings, "live_keys.show_most_used_title", true);
			obs_data_set_default_string(settings, "live_keys.most_used_title", "Most Used Keys");
			obs_data_set_default_int(settings, "live_keys.fade_ms", 2000);
			obs_data_set_default_string(settings, "live_keys.fade_curve", "ease_out");
			obs_data_set_default_string(settings, "live_keys.blacklist", "");
			obs_data_set_default_int(settings, "live_keys.color", 0xffeb6325);
			obs_data_set_default_int(settings, "live_keys.pressed_color", 0xff4444ef);
		} else if constexpr (std::is_same_v<T, mouse_activity_source>) {
			obs_data_set_default_int(settings, "activity.width", 720);
			obs_data_set_default_int(settings, "activity.height", 560);
			obs_data_set_default_string(settings, "activity.title", "Mouse Activity");
			obs_data_set_default_string(settings, "mouse_activity.left_label", "L");
			obs_data_set_default_string(settings, "mouse_activity.right_label", "R");
			obs_data_set_default_string(settings, "mouse_activity.middle_label", "M");
			obs_data_set_default_bool(settings, "mouse_activity.show_heatmap", true);
			obs_data_set_default_bool(settings, "mouse_activity.show_live_mouse", true);
			obs_data_set_default_bool(settings, "mouse_activity.show_distance", false);
			obs_data_set_default_bool(settings, "mouse_activity.show_clicks", false);
			obs_data_set_default_string(settings, "mouse_activity.summary_position", "top");
			obs_data_set_default_string(settings, "mouse_activity.distance_unit", "pixels");
			obs_data_set_default_int(settings, "mouse_activity.mouse_dpi", 800);
			obs_data_set_default_bool(settings, "mouse_activity.show_border", false);
			obs_data_set_default_bool(settings, "mouse_activity.show_center_mark", false);
			obs_data_set_default_int(settings, "mouse_activity.trail_ms", 1500);
			obs_data_set_default_string(settings, "mouse_activity.heatmap_gradient", "spectrum");
			obs_data_set_default_int(settings, "mouse_activity.custom_gradient_low", 0xffeb6325);
			obs_data_set_default_int(settings, "mouse_activity.custom_gradient_middle", 0xff15ccfa);
			obs_data_set_default_int(settings, "mouse_activity.custom_gradient_high", 0xff4444ef);
			obs_data_set_default_int(settings, "mouse_activity.hex_size",
						 static_cast<int64_t>(default_heatmap_hex_radius));
			obs_data_set_default_int(settings, "mouse_activity.opacity", 100);
			obs_data_set_default_string(
				settings, "mouse_activity.export_directory",
				QStandardPaths::writableLocation(QStandardPaths::PicturesLocation).toUtf8().constData());
			obs_data_set_default_string(settings, "mouse_activity.export_format", "png");
			obs_data_set_default_int(settings, "mouse_activity.color", 0xffeb6325);
			obs_data_set_default_int(settings, "mouse_activity.left_color", 0xffeb6325);
			obs_data_set_default_int(settings, "mouse_activity.right_color", 0xff4444ef);
			obs_data_set_default_int(settings, "mouse_activity.middle_color", 0xff15ccfa);
			obs_data_set_default_string(settings, "mouse_activity.map", "movement");
			obs_data_set_default_string(settings, "mouse_activity.region", "display");
			obs_data_set_default_int(settings, "mouse_activity.region.left", 0);
			obs_data_set_default_int(settings, "mouse_activity.region.top", 0);
			obs_data_set_default_int(settings, "mouse_activity.region.right", 1919);
			obs_data_set_default_int(settings, "mouse_activity.region.bottom", 1079);
		} else if constexpr (std::is_same_v<T, statistics_source>) {
			obs_data_set_default_int(settings, "activity.width", 480);
			obs_data_set_default_int(settings, "activity.height", 240);
			obs_data_set_default_string(settings, "activity.title", "Input Statistics");
			obs_data_set_default_bool(settings, "statistics.show_keys", true);
			obs_data_set_default_bool(settings, "statistics.show_clicks", true);
			obs_data_set_default_bool(settings, "statistics.show_actions", true);
			obs_data_set_default_int(settings, "statistics.theme_color", 0xffeb6325);
			obs_data_set_default_int(settings, "statistics.pressed_color", 0xff4444ef);
			obs_data_set_default_int(settings, "statistics.horizontal_spacing", 10);
			obs_data_set_default_int(settings, "statistics.vertical_spacing", 10);
			obs_data_set_default_int(settings, "statistics.group_spacing", 10);
			obs_data_set_default_string(settings, "statistics.alignment", "left");
			obs_data_set_default_bool(settings, "statistics.show_lap_keys", false);
			obs_data_set_default_bool(settings, "statistics.show_lap_clicks", false);
			obs_data_set_default_bool(settings, "statistics.show_lap_actions", false);
		} else if constexpr (std::is_same_v<T, input_intensity_source>) {
			obs_data_set_default_string(settings, "activity.title", "Input Intensity");
			obs_data_set_default_int(settings, "input_intensity.window", 30);
			obs_data_set_default_int(settings, "input_intensity.metric_count", 2);
			obs_data_set_default_int(settings, "input_intensity.element_spacing", 10);
			obs_data_set_default_string(settings, "input_intensity.layout", "column");
			obs_data_set_default_string(settings, "input_intensity.velocity_unit", "pixels");
			obs_data_set_default_int(settings, "input_intensity.mouse_dpi", 800);
			obs_data_set_default_int(settings, "input_intensity.theme_color", 0xffeb6325);
			obs_data_set_default_int(settings, "input_intensity.pressed_color", 0xff4444ef);
			for (size_t index = 0; index < intensity_max_fields; ++index) {
				const std::string prefix = "input_intensity.row" + std::to_string(index) + ".";
				obs_data_set_default_bool(settings, (prefix + "enabled").c_str(), index == 0);
				obs_data_set_default_string(settings, (prefix + "metric").c_str(), "actions");
				obs_data_set_default_int(settings, (prefix + "key").c_str(), VC_SPACE);
				obs_data_set_default_int(settings, (prefix + "button").c_str(), MOUSE_BUTTON1);
				obs_data_set_default_string(settings, (prefix + "title").c_str(), "");
				obs_data_set_default_string(settings, (prefix + "key_scope").c_str(), "all");
				obs_data_set_default_string(settings, (prefix + "key_list").c_str(), "");
			}
		}
	};
	obs_register_source(&info);
}
