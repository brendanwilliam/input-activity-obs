class unified_source final {
public:
	unified_source(obs_source_t *source, obs_data_t *settings) : source(source)
	{
		common_settings = obs_data_create();
		copy_shared_settings(common_settings, settings);
		for (auto &saved : mode_settings) {
			saved = obs_data_create();
			obs_data_apply(saved, settings);
		}
		obs_data_set_string(mode_settings[mode_index(source_mode::live_keys)], "activity.title", "Live Keys");
		obs_data_set_bool(mode_settings[mode_index(source_mode::live_keys)], "activity.show_title", false);
		obs_data_set_string(mode_settings[mode_index(source_mode::mouse_activity)], "activity.title",
				    "Mouse Activity");
		obs_data_set_string(mode_settings[mode_index(source_mode::input_intensity)], "activity.title",
				    "Input Intensity");
		obs_data_set_string(mode_settings[mode_index(source_mode::input_statistics)], "activity.title",
				    "Input Statistics");
		live_keys = std::make_unique<live_keys_source>(source, settings, false, false);
		mouse_activity = std::make_unique<mouse_activity_source>(source, settings, false, false);
		input_intensity = std::make_unique<input_intensity_source>(source, settings, false, false);
		statistics = std::make_unique<statistics_source>(source, settings, false, false);
		reset_hotkey = obs_hotkey_register_source(
			source, "reset_input_activity", obs_module_text("Activity.ResetHotkey"),
			[](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
				if (pressed)
					static_cast<unified_source *>(data)->active()->reset_activity();
			},
			this);
		lap_hotkey = obs_hotkey_register_source(
			source, "lap_input_activity", obs_module_text("Activity.LapHotkey"),
			[](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
				if (pressed)
					static_cast<unified_source *>(data)->active()->lap_activity();
			},
			this);
		export_hotkey = obs_hotkey_register_source(
			source, "export_mouse_heatmap", obs_module_text("MouseActivity.ExportHotkey"),
			[](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
				auto *unified = static_cast<unified_source *>(data);
				if (pressed && unified->mode == source_mode::mouse_activity)
					unified->mouse_activity->export_current_heatmap();
			},
			this);
		update(settings);
	}
	~unified_source()
	{
		obs_hotkey_unregister(reset_hotkey);
		obs_hotkey_unregister(lap_hotkey);
		obs_hotkey_unregister(export_hotkey);
		for (auto *saved : mode_settings)
			obs_data_release(saved);
		obs_data_release(common_settings);
	}
	void update(obs_data_t *settings)
	{
		migrate_shared_settings(settings);
		const std::string selected = obs_data_get_string(settings, "input_activity.mode");
		const source_mode selected_mode = selected == "mouse_activity"     ? source_mode::mouse_activity
						  : selected == "input_intensity"  ? source_mode::input_intensity
						  : selected == "input_statistics" ? source_mode::input_statistics
										   : source_mode::live_keys;
		if (configured) {
			obs_data_apply(mode_settings[mode_index(mode)], settings);
			copy_shared_settings(common_settings, settings);
		}
		if (configured && selected_mode != mode) {
			obs_data_apply(settings, mode_settings[mode_index(selected_mode)]);
			copy_shared_settings(settings, common_settings);
			obs_data_set_string(settings, "input_activity.mode", selected.c_str());
		}
		mode = selected_mode;
		configured = true;
		apply_mode_title_settings(settings);
		active()->update(settings);
	}
	void tick(float seconds) { active()->tick(seconds); }
	void draw(gs_effect_t *effect) { active()->draw(effect); }
	uint32_t width() const { return static_cast<uint32_t>(active()->width); }
	uint32_t height() const { return static_cast<uint32_t>(active()->height); }
	void clear_mouse_activity() { mouse_activity->clear(); }
	mouse_activity_source *mouse() const { return mouse_activity.get(); }
	void set_properties_visibility(obs_properties_t *properties) const;

private:
	enum class source_mode { live_keys, mouse_activity, input_intensity, input_statistics };
	static size_t mode_index(source_mode value) { return static_cast<size_t>(value); }
	static void copy_shared_settings(obs_data_t *destination, obs_data_t *source)
	{
		const char *const integer_keys[] = {"activity.width",
						    "activity.height",
						    "activity.padding",
						    "activity.font_size",
						    "activity.text_color",
						    "activity.background_color",
						    "activity.text_shadow_color",
						    "activity.text_shadow_offset",
						    "activity.target.display",
						    "activity.target.rectangle.left",
						    "activity.target.rectangle.top",
						    "activity.target.rectangle.right",
						    "activity.target.rectangle.bottom",
						    "activity.inactive_color",
						    "activity.active_color",
						    "activity.within_spacing",
						    "activity.group_spacing",
						    "activity.section_title_size",
						    "activity.input_title_size",
						    "activity.input_value_size"};
		for (const char *key : integer_keys)
			obs_data_set_int(destination, key, obs_data_get_int(source, key));
		obs_data_set_bool(destination, "activity.text_shadow",
				  obs_data_get_bool(source, "activity.text_shadow"));
		obs_data_set_bool(destination, "input_activity.show_advanced_settings",
				  obs_data_get_bool(source, "input_activity.show_advanced_settings"));
		obs_data_set_bool(destination, "activity.target.display_rectangle",
				  obs_data_get_bool(source, "activity.target.display_rectangle"));
		const char *const string_keys[] = {"activity.target.type", "activity.target.application",
						   "activity.target.window"};
		for (const char *key : string_keys)
			obs_data_set_string(destination, key, obs_data_get_string(source, key));
		if (auto *font = obs_data_get_obj(source, "activity.font")) {
			obs_data_set_obj(destination, "activity.font", font);
			obs_data_release(font);
		}
	}
	static void migrate_shared_settings(obs_data_t *settings)
	{
		if (obs_data_get_bool(settings, "activity.shared_settings_migrated"))
			return;
		const std::string mode = obs_data_get_string(settings, "input_activity.mode");
		const char *inactive = mode == "live_keys"          ? "live_keys.color"
				       : mode == "input_statistics" ? "statistics.theme_color"
				       : mode == "input_intensity"  ? "input_intensity.theme_color"
								    : "mouse_activity.color";
		const char *active = mode == "live_keys"          ? "live_keys.pressed_color"
				     : mode == "input_statistics" ? "statistics.pressed_color"
				     : mode == "input_intensity"  ? "input_intensity.pressed_color"
								  : "mouse_activity.color";
		if (obs_data_has_user_value(settings, inactive))
			obs_data_set_int(settings, "activity.inactive_color", obs_data_get_int(settings, inactive));
		if (obs_data_has_user_value(settings, active))
			obs_data_set_int(settings, "activity.active_color", obs_data_get_int(settings, active));
		if (mode == "mouse_activity" && !obs_data_has_user_value(settings, "activity.target.type")) {
			obs_data_set_string(settings, "activity.target.type", "display");
			obs_data_set_int(settings, "activity.target.display",
					 obs_data_get_int(settings, "mouse_activity.display"));
			if (std::string(obs_data_get_string(settings, "mouse_activity.region")) == "custom") {
				obs_data_set_bool(settings, "activity.target.display_rectangle", true);
				for (const char *edge : {"left", "top", "right", "bottom"}) {
					const std::string old_key = std::string("mouse_activity.region.") + edge;
					const std::string new_key = std::string("activity.target.rectangle.") + edge;
					obs_data_set_int(settings, new_key.c_str(),
							 obs_data_get_int(settings, old_key.c_str()));
				}
			}
		}
		obs_data_set_bool(settings, "activity.shared_settings_migrated", true);
	}
	void apply_mode_title_settings(obs_data_t *settings) const
	{
		const char *prefix = mode == source_mode::mouse_activity     ? "mouse_activity"
				     : mode == source_mode::input_intensity  ? "input_intensity"
				     : mode == source_mode::input_statistics ? "statistics"
									     : nullptr;
		if (!prefix)
			return;
		const std::string setting_prefix = std::string(prefix) + ".";
		obs_data_set_bool(settings, "activity.show_title",
				  obs_data_get_bool(settings, (setting_prefix + "show_title").c_str()));
		obs_data_set_string(settings, "activity.title",
				    obs_data_get_string(settings, (setting_prefix + "title").c_str()));
	}
	activity_source *active() const
	{
		switch (mode) {
		case source_mode::mouse_activity:
			return mouse_activity.get();
		case source_mode::input_intensity:
			return input_intensity.get();
		case source_mode::input_statistics:
			return statistics.get();
		case source_mode::live_keys:
			return live_keys.get();
		}
		return live_keys.get();
	}
	obs_source_t *source{};
	obs_hotkey_id reset_hotkey = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id lap_hotkey = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id export_hotkey = OBS_INVALID_HOTKEY_ID;
	source_mode mode{source_mode::live_keys};
	std::unique_ptr<live_keys_source> live_keys;
	std::unique_ptr<mouse_activity_source> mouse_activity;
	std::unique_ptr<input_intensity_source> input_intensity;
	std::unique_ptr<statistics_source> statistics;
	std::array<obs_data_t *, 4> mode_settings{};
	obs_data_t *common_settings{};
	bool configured{};
};

bool target_type_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings);
bool intensity_metric_count_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings);
bool intensity_behavior_options_changed(void *behavior_data, obs_properties_t *, obs_property_t *,
					obs_data_t *settings);

void set_advanced_properties_visibility(obs_properties_t *properties, bool show_advanced)
{
	for (const char *name : {"input_activity.general.typography", "input_activity.live_keys.layout",
				 "input_activity.live_keys.typography", "input_activity.live_keys.behavior",
				 "input_activity.mouse_activity.behavior", "input_activity.mouse_activity.actions",
				 "input_activity.input_intensity.layout", "input_activity.input_intensity.behavior",
				 "input_activity.input_statistics.layout", "input_activity.input_statistics.typography",
				 "input_activity.input_statistics.behavior"})
		obs_property_set_visible(obs_properties_get(properties, name), show_advanced);
	for (const char *name :
	     {"activity.padding", "activity.text_shadow", "activity.text_shadow_color", "activity.text_shadow_offset"})
		obs_property_set_visible(obs_properties_get(properties, name), show_advanced);
}

void unified_source::set_properties_visibility(obs_properties_t *properties) const
{
	auto *settings = obs_source_get_settings(source);
	const std::string selected = obs_data_get_string(settings, "input_activity.mode");
	const bool show_advanced = obs_data_get_bool(settings, "input_activity.show_advanced_settings");
	obs_properties_apply_settings(properties, settings);
	target_type_changed(properties, nullptr, settings);
	auto *intensity_group = obs_properties_get(properties, "input_activity.input_intensity");
	auto *intensity_contents = intensity_group ? obs_property_group_content(intensity_group) : nullptr;
	auto *intensity_metrics =
		intensity_contents ? obs_properties_get(intensity_contents, "input_activity.input_intensity.metrics")
				   : nullptr;
	if (intensity_metrics)
		intensity_metric_count_changed(obs_property_group_content(intensity_metrics), nullptr, settings);
	auto *intensity_behavior =
		intensity_contents ? obs_properties_get(intensity_contents, "input_activity.input_intensity.behavior")
				   : nullptr;
	intensity_behavior_options_changed(intensity_behavior ? obs_property_group_content(intensity_behavior)
							      : nullptr,
					   nullptr, nullptr, settings);
	obs_data_release(settings);
	obs_property_set_visible(obs_properties_get(properties, "input_activity.live_keys"), selected == "live_keys");
	obs_property_set_visible(obs_properties_get(properties, "input_activity.mouse_activity"),
				 selected == "mouse_activity");
	obs_property_set_visible(obs_properties_get(properties, "input_activity.input_intensity"),
				 selected == "input_intensity");
	obs_property_set_visible(obs_properties_get(properties, "input_activity.input_statistics"),
				 selected == "input_statistics");
	set_advanced_properties_visibility(properties, show_advanced);
}

bool target_type_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const std::string target_type = obs_data_get_string(settings, "activity.target.type");
	obs_property_set_visible(obs_properties_get(props, "activity.target.display"), target_type == "display");
	obs_property_set_visible(obs_properties_get(props, "activity.target.application"),
				 target_type == "application");
	obs_property_set_visible(obs_properties_get(props, "activity.target.window"), target_type == "window");
	const bool rectangle = target_type == "display" &&
			       obs_data_get_bool(settings, "activity.target.display_rectangle");
	for (const char *name :
	     {"activity.target.display_rectangle", "activity.target.rectangle.left", "activity.target.rectangle.top",
	      "activity.target.rectangle.right", "activity.target.rectangle.bottom"})
		obs_property_set_visible(obs_properties_get(props, name),
					 std::string(name) == "activity.target.display_rectangle" || rectangle);
	return true;
}

obs_properties_t *add_group(obs_properties_t *parent, const char *name, const char *description)
{
	auto *contents = obs_properties_create();
	obs_properties_add_group(parent, name, description, OBS_GROUP_NORMAL, contents);
	return contents;
}
