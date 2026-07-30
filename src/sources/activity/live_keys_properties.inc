bool live_keys_options_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool chart = obs_data_get_bool(settings, "live_keys.show_most_used");
	const bool live_title = obs_data_get_bool(settings, "live_keys.show_live_title");
	const bool chart_title = chart && obs_data_get_bool(settings, "live_keys.show_most_used_title");
	for (const char *name : {"live_keys.top_n", "live_keys.top_n_alignment", "live_keys.most_used_element_spacing",
				 "live_keys.group_spacing", "live_keys.layout_order", "live_keys.show_most_used_title",
				 "live_keys.most_used_font_size"})
		obs_property_set_visible(obs_properties_get(props, name), chart);
	obs_property_set_visible(obs_properties_get(props, "live_keys.live_title"), live_title);
	obs_property_set_visible(obs_properties_get(props, "live_keys.live_title_font_size"), live_title);
	obs_property_set_visible(obs_properties_get(props, "live_keys.most_used_title"), chart_title);
	obs_property_set_visible(obs_properties_get(props, "live_keys.most_used_title_font_size"), chart_title);
	return true;
}

obs_properties_t *keys_properties_impl(void *, bool include_common)
{
	auto *p = obs_properties_create();
	if (include_common)
		add_common_properties(p);
	auto *content = add_group(p, "input_activity.live_keys.content", obs_module_text("Preferences.Content"));
	obs_properties_add_int(content, "live_keys.maximum", obs_module_text("LiveKeys.Maximum"), 1, 64, 1);
	obs_properties_add_int(content, "live_keys.row_height", obs_module_text("LiveKeys.RowHeight"), 24, 2160, 1);
	auto *show_live_title = obs_properties_add_bool(content, "live_keys.show_live_title",
							obs_module_text("LiveKeys.ShowLiveTitle"));
	obs_property_set_modified_callback(show_live_title, live_keys_options_changed);
	obs_properties_add_int(content, "live_keys.top_n", obs_module_text("LiveKeys.TopN"), 1, 64, 1);
	auto *show_most_used =
		obs_properties_add_bool(content, "live_keys.show_most_used", obs_module_text("LiveKeys.ShowMostUsed"));
	obs_property_set_modified_callback(show_most_used, live_keys_options_changed);
	auto *layout = add_group(p, "input_activity.live_keys.layout", obs_module_text("Preferences.Layout"));
	auto *top_n_alignment = obs_properties_add_list(layout, "live_keys.top_n_alignment",
							obs_module_text("LiveKeys.TopNAlignment"), OBS_COMBO_TYPE_LIST,
							OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(top_n_alignment, obs_module_text("LiveKeys.TopNAlignment.Left"), "left");
	obs_property_list_add_string(top_n_alignment, obs_module_text("LiveKeys.TopNAlignment.Right"), "right");
	obs_properties_add_int_slider(layout, "live_keys.horizontal_spacing",
				      obs_module_text("LiveKeys.HorizontalSpacing"), 0, 200, 1);
	obs_properties_add_int_slider(layout, "live_keys.vertical_spacing", obs_module_text("LiveKeys.VerticalSpacing"),
				      -100, 200, 1);
	auto *total_position = obs_properties_add_list(layout, "live_keys.total_position",
						       obs_module_text("LiveKeys.TotalPosition"), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(total_position, obs_module_text("LiveKeys.TotalPosition.Above"), "above");
	obs_property_list_add_string(total_position, obs_module_text("LiveKeys.TotalPosition.Below"), "below");
	obs_properties_add_int_slider(layout, "live_keys.most_used_element_spacing",
				      obs_module_text("LiveKeys.MostUsedSpacing"), 0, 200, 1);
	obs_properties_add_int_slider(layout, "live_keys.group_spacing",
				      obs_module_text("LiveKeys.BetweenGroupSpacing"), 0, 200, 1);
	auto *layout_order = obs_properties_add_list(layout, "live_keys.layout_order",
						     obs_module_text("LiveKeys.LayoutOrder"), OBS_COMBO_TYPE_LIST,
						     OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(layout_order, obs_module_text("LiveKeys.LayoutOrder.LiveFirst"), "live_first");
	obs_property_list_add_string(layout_order, obs_module_text("LiveKeys.LayoutOrder.ChartFirst"), "chart_first");
	auto *typography =
		add_group(p, "input_activity.live_keys.typography", obs_module_text("Preferences.Typography"));
	obs_properties_add_text(typography, "live_keys.live_title", obs_module_text("LiveKeys.LiveTitle"),
				OBS_TEXT_DEFAULT);
	obs_properties_add_int(typography, "live_keys.live_title_font_size", obs_module_text("LiveKeys.TitleFontSize"),
			       8, 256, 1);
	auto *show_most_used_title = obs_properties_add_bool(typography, "live_keys.show_most_used_title",
							     obs_module_text("LiveKeys.ShowMostUsedTitle"));
	obs_property_set_modified_callback(show_most_used_title, live_keys_options_changed);
	obs_properties_add_text(typography, "live_keys.most_used_title", obs_module_text("LiveKeys.MostUsedTitle"),
				OBS_TEXT_DEFAULT);
	obs_properties_add_int(typography, "live_keys.most_used_title_font_size",
			       obs_module_text("LiveKeys.TitleFontSize"), 8, 256, 1);
	obs_properties_add_int(typography, "live_keys.key_font_size", obs_module_text("LiveKeys.KeyFontSize"), 8, 256,
			       1);
	obs_properties_add_int(typography, "live_keys.special_key_font_size",
			       obs_module_text("LiveKeys.SpecialKeyFontSize"), 8, 256, 1);
	obs_properties_add_int(typography, "live_keys.total_font_size", obs_module_text("LiveKeys.TotalFontSize"), 8,
			       256, 1);
	obs_properties_add_int(typography, "live_keys.most_used_font_size",
			       obs_module_text("LiveKeys.MostUsedFontSize"), 8, 256, 1);
	auto *behavior = add_group(p, "input_activity.live_keys.behavior", obs_module_text("Preferences.Behavior"));
	obs_properties_add_text(behavior, "live_keys.blacklist", obs_module_text("LiveKeys.Blacklist"),
				OBS_TEXT_DEFAULT);
	obs_properties_add_int_slider(behavior, "live_keys.fade_ms", obs_module_text("LiveKeys.FadeDuration"), 0, 5000,
				      10);
	auto *fade_curve = obs_properties_add_list(behavior, "live_keys.fade_curve",
						   obs_module_text("LiveKeys.FadeCurve"), OBS_COMBO_TYPE_LIST,
						   OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(fade_curve, obs_module_text("LiveKeys.FadeCurve.Linear"), "linear");
	obs_property_list_add_string(fade_curve, obs_module_text("LiveKeys.FadeCurve.EaseIn"), "ease_in");
	obs_property_list_add_string(fade_curve, obs_module_text("LiveKeys.FadeCurve.EaseOut"), "ease_out");
	obs_property_list_add_string(fade_curve, obs_module_text("LiveKeys.FadeCurve.EaseInOut"), "ease_in_out");
	return p;
}
obs_properties_t *keys_properties(void *data)
{
	return keys_properties_impl(data, true);
}

