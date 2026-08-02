
void add_intensity_key_list(obs_property_t *list)
{
	for (uint16_t code = VC_A; code <= VC_Z; ++code) {
		input_data::trace_event event{};
		event.code = code;
		const QByteArray label = key_name(event).toUtf8();
		obs_property_list_add_int(list, label.constData(), code);
	}
	for (uint16_t code = VC_0; code <= VC_9; ++code) {
		input_data::trace_event event{};
		event.code = code;
		const QByteArray label = key_name(event).toUtf8();
		obs_property_list_add_int(list, label.constData(), code);
	}
	const uint16_t common_codes[] = {VC_SPACE,     VC_ENTER,     VC_ESCAPE,       VC_TAB,           VC_BACKSPACE,
					 VC_SHIFT_L,   VC_CONTROL_L, VC_ALT_L,        VC_META_L,        VC_CAPS_LOCK,
					 VC_UP,        VC_DOWN,      VC_LEFT,         VC_RIGHT,         VC_HOME,
					 VC_END,       VC_PAGE_UP,   VC_PAGE_DOWN,    VC_INSERT,        VC_DELETE,
					 VC_MINUS,     VC_EQUALS,    VC_OPEN_BRACKET, VC_CLOSE_BRACKET, VC_BACK_SLASH,
					 VC_SEMICOLON, VC_QUOTE,     VC_COMMA,        VC_PERIOD,        VC_SLASH,
					 VC_BACK_QUOTE};
	for (const uint16_t code : common_codes) {
		input_data::trace_event event{};
		event.code = code;
		const QByteArray label = key_name(event).toUtf8();
		obs_property_list_add_int(list, label.constData(), code);
	}
	for (uint16_t code = VC_F1; code <= VC_F12; ++code) {
		input_data::trace_event event{};
		event.code = code;
		const QByteArray label = key_name(event).toUtf8();
		obs_property_list_add_int(list, label.constData(), code);
	}
	for (uint16_t code = VC_F13; code <= VC_F24; ++code) {
		input_data::trace_event event{};
		event.code = code;
		const QByteArray label = key_name(event).toUtf8();
		obs_property_list_add_int(list, label.constData(), code);
	}
	for (uint16_t code = VC_KP_0; code <= VC_KP_9; ++code) {
		input_data::trace_event event{};
		event.code = code;
		const QByteArray label = key_name(event).toUtf8();
		obs_property_list_add_int(list, label.constData(), code);
	}
	const uint16_t keypad_codes[] = {VC_KP_DIVIDE, VC_KP_MULTIPLY, VC_KP_SUBTRACT,
					 VC_KP_ADD,    VC_KP_DECIMAL,  VC_KP_ENTER};
	for (const uint16_t code : keypad_codes) {
		input_data::trace_event event{};
		event.code = code;
		const QByteArray label = key_name(event).toUtf8();
		obs_property_list_add_int(list, label.constData(), code);
	}
}

bool intensity_metric_count_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const int count = std::clamp(static_cast<int>(obs_data_get_int(settings, "input_intensity.metric_count")), 1,
				     static_cast<int>(intensity_max_fields));
	for (size_t index = 0; index < intensity_max_fields; ++index) {
		const std::string prefix = "input_intensity.row" + std::to_string(index) + ".";
		for (const char *suffix : {"enabled", "title", "metric"})
			obs_property_set_visible(obs_properties_get(props, (prefix + suffix).c_str()),
						 index < static_cast<size_t>(count));
	}
	return true;
}

bool intensity_behavior_options_changed(void *behavior_data, obs_properties_t *, obs_property_t *, obs_data_t *settings)
{
	auto *behavior = static_cast<obs_properties_t *>(behavior_data);
	if (!behavior)
		return true;
	const int count = std::clamp(static_cast<int>(obs_data_get_int(settings, "input_intensity.metric_count")), 1,
				     static_cast<int>(intensity_max_fields));
	bool velocity_enabled{};
	for (size_t index = 0; index < intensity_max_fields; ++index) {
		const std::string prefix = "input_intensity.row" + std::to_string(index) + ".";
		const bool active = index < static_cast<size_t>(count) &&
				    obs_data_get_bool(settings, (prefix + "enabled").c_str());
		const std::string metric = obs_data_get_string(settings, (prefix + "metric").c_str());
		const bool keyboard = active && metric == "keyboard";
		const bool key = active && metric == "key";
		const bool button = active && metric == "button";
		velocity_enabled = velocity_enabled || (active && metric == "velocity");
		obs_property_set_visible(obs_properties_get(behavior, (prefix + "key_scope").c_str()), keyboard);
		obs_property_set_visible(obs_properties_get(behavior, (prefix + "key_list").c_str()),
					 keyboard && std::string(obs_data_get_string(
							     settings, (prefix + "key_scope").c_str())) == "list");
		obs_property_set_visible(obs_properties_get(behavior, (prefix + "key").c_str()), key);
		obs_property_set_visible(obs_properties_get(behavior, (prefix + "button").c_str()), button);
	}
	obs_property_set_visible(obs_properties_get(behavior, "input_intensity.velocity_unit"), velocity_enabled);
	obs_property_set_visible(obs_properties_get(behavior, "input_intensity.mouse_dpi"), velocity_enabled);
	return true;
}

bool intensity_metric_count_behavior_changed(void *behavior_data, obs_properties_t *props, obs_property_t *property,
					     obs_data_t *settings)
{
	intensity_metric_count_changed(props, property, settings);
	return intensity_behavior_options_changed(behavior_data, props, property, settings);
}

obs_properties_t *intensity_properties_impl(void *, bool include_common, const char *title_prefix = nullptr)
{
	auto *p = obs_properties_create();
	if (include_common)
		add_common_properties(p);
	auto *metrics = add_group(p, "input_activity.input_intensity.metrics", obs_module_text("Preferences.Metrics"));
	add_mode_title_properties(metrics, title_prefix);
	obs_properties_add_int_slider(metrics, "input_intensity.window", obs_module_text("InputIntensity.Window"), 1,
				      60, 1);
	auto *metric_count = obs_properties_add_int_slider(metrics, "input_intensity.metric_count",
							   obs_module_text("InputIntensity.MetricCount"), 1,
							   intensity_max_fields, 1);
	for (size_t index = 0; index < intensity_max_fields; ++index) {
		const std::string prefix = "input_intensity.row" + std::to_string(index) + ".";
		const QByteArray row_label =
			QString("%1 %2").arg(obs_module_text("InputIntensity.Row")).arg(index + 1).toUtf8();
		obs_properties_add_bool(metrics, (prefix + "enabled").c_str(), row_label.constData());
		obs_properties_add_text(metrics, (prefix + "title").c_str(), obs_module_text("InputIntensity.Title"),
					OBS_TEXT_DEFAULT);
		auto *metric = obs_properties_add_list(metrics, (prefix + "metric").c_str(),
						       obs_module_text("InputIntensity.Metric"), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_STRING);
		obs_property_list_add_string(metric, obs_module_text("InputIntensity.Metric.Actions"), "actions");
		obs_property_list_add_string(metric, obs_module_text("InputIntensity.Metric.Keyboard"), "keyboard");
		obs_property_list_add_string(metric, obs_module_text("InputIntensity.Metric.Mouse"), "mouse");
		obs_property_list_add_string(metric, obs_module_text("InputIntensity.Metric.Key"), "key");
		obs_property_list_add_string(metric, obs_module_text("InputIntensity.Metric.MouseButton"), "button");
		obs_property_list_add_string(metric, obs_module_text("InputIntensity.Metric.Velocity"), "velocity");
	}
	auto *layout = add_group(p, "input_activity.input_intensity.layout", obs_module_text("Preferences.Layout"));
	obs_properties_add_int_slider(layout, "input_intensity.element_spacing",
				      obs_module_text("Activity.ElementSpacing"), 0, 200, 1);
	auto *layout_type = obs_properties_add_list(layout, "input_intensity.layout",
						    obs_module_text("InputIntensity.Layout"), OBS_COMBO_TYPE_LIST,
						    OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(layout_type, obs_module_text("InputIntensity.Layout.Column"), "column");
	obs_property_list_add_string(layout_type, obs_module_text("InputIntensity.Layout.Row"), "row");
	auto *behavior =
		add_group(p, "input_activity.input_intensity.behavior", obs_module_text("Preferences.Behavior"));
	obs_property_set_modified_callback2(metric_count, intensity_metric_count_behavior_changed, behavior);
	auto *velocity_unit = obs_properties_add_list(behavior, "input_intensity.velocity_unit",
						      obs_module_text("InputIntensity.VelocityUnit"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(velocity_unit, obs_module_text("InputIntensity.VelocityUnit.Pixels"), "pixels");
	obs_property_list_add_string(velocity_unit, obs_module_text("InputIntensity.VelocityUnit.Metric"), "metric");
	obs_property_list_add_string(velocity_unit, obs_module_text("InputIntensity.VelocityUnit.Imperial"),
				     "imperial");
	obs_properties_add_int(behavior, "input_intensity.mouse_dpi", obs_module_text("InputIntensity.MouseDPI"), 1,
			       100000, 1);
	for (size_t index = 0; index < intensity_max_fields; ++index) {
		const std::string prefix = "input_intensity.row" + std::to_string(index) + ".";
		auto *key_scope = obs_properties_add_list(behavior, (prefix + "key_scope").c_str(),
							  obs_module_text("InputIntensity.KeyScope"),
							  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_list_add_string(key_scope, obs_module_text("InputIntensity.KeyScope.All"), "all");
		obs_property_list_add_string(key_scope, obs_module_text("InputIntensity.KeyScope.Letters"), "letters");
		obs_property_list_add_string(key_scope, obs_module_text("InputIntensity.KeyScope.Numbers"), "numbers");
		obs_property_list_add_string(key_scope, obs_module_text("InputIntensity.KeyScope.List"), "list");
		obs_property_set_modified_callback2(key_scope, intensity_behavior_options_changed, behavior);
		obs_properties_add_text(behavior, (prefix + "key_list").c_str(),
					obs_module_text("InputIntensity.KeyList"), OBS_TEXT_DEFAULT);
		auto *key = obs_properties_add_list(behavior, (prefix + "key").c_str(),
						    obs_module_text("InputIntensity.Key"), OBS_COMBO_TYPE_LIST,
						    OBS_COMBO_FORMAT_INT);
		add_intensity_key_list(key);
		auto *button = obs_properties_add_list(behavior, (prefix + "button").c_str(),
						       obs_module_text("InputIntensity.Button"), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_INT);
		for (uint16_t code = MOUSE_BUTTON1; code <= MOUSE_BUTTON5; ++code) {
			const QByteArray label = QString("%1 %2")
							 .arg(obs_module_text("InputIntensity.Metric.MouseButton"))
							 .arg(code)
							 .toUtf8();
			obs_property_list_add_int(button, label.constData(), code);
		}
		obs_property_set_modified_callback2(obs_properties_get(metrics, (prefix + "enabled").c_str()),
						    intensity_behavior_options_changed, behavior);
		obs_property_set_modified_callback2(obs_properties_get(metrics, (prefix + "metric").c_str()),
						    intensity_behavior_options_changed, behavior);
	}
	return p;
}
obs_properties_t *intensity_properties(void *data)
{
	return intensity_properties_impl(data, true);
}
