bool export_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	return static_cast<game_report_source *>(data)->export_selected();
}
bool delete_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	return static_cast<game_report_source *>(data)->delete_selected();
}
bool enrich_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	return static_cast<game_report_source *>(data)->enrich_selected();
}
bool reveal_development_log_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	return data && static_cast<game_report_source *>(data)->reveal_development_log();
}
bool latest_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	obs_property_set_visible(obs_properties_get(props, selected_key), !obs_data_get_bool(settings, latest_key));
	return true;
}
obs_properties_t *properties(void *data)
{
	auto *props = obs_properties_create();
	auto *general = obs_properties_create();
	auto *latest = obs_properties_add_bool(general, latest_key, obs_module_text("LoLGameReport.ShowLatest"));
	auto *list = obs_properties_add_list(general, selected_key, obs_module_text("LoLGameReport.Selected"),
					     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	if (data)
		for (const auto &report : static_cast<game_report_source *>(data)->reports())
			obs_property_list_add_string(list, lol_game_report::display_name(report).toUtf8().constData(),
						     report.id.toUtf8().constData());
	const QString status = QString("%1: %2").arg(
		obs_module_text("LoLGameReport.CollectorStatus"),
		data ? static_cast<game_report_source *>(data)->collector_status()
		     : lol_game_report::collector::state_text(lol_game_report::collection_state::empty));
	obs_properties_add_text(general, "lol_game_report.collector_status", status.toUtf8().constData(),
				OBS_TEXT_INFO);
	auto *page = obs_properties_add_list(general, page_key, obs_module_text("LoLGameReport.Page"),
					     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(page, obs_module_text("LoLGameReport.Page.Summary"), "summary");
	obs_property_list_add_string(page, obs_module_text("LoLGameReport.Page.Performance"), "performance");
	obs_property_list_add_string(page, obs_module_text("LoLGameReport.Page.Game"), "game");
	obs_properties_add_bool(general, normalize_key, obs_module_text("LoLGameReport.NormalizeAverage"));
	auto *series = obs_properties_add_list(general, series_key, obs_module_text("LoLGameReport.ChartSeries"),
					       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(series, "Level progression", "level");
	obs_properties_add_text(general, events_key, obs_module_text("LoLGameReport.EventCategories"),
				OBS_TEXT_DEFAULT);
	obs_properties_add_int(general, dpi_key, obs_module_text("LoLGameReport.MouseDPI"), 100, 32000, 50);
	obs_properties_add_int(general, hex_radius_key, obs_module_text("LoLGameReport.HexSize"), 1, 20, 1);
	obs_properties_add_path(general, game_cfg_key, obs_module_text("LeagueSafeArea.GameCfg"), OBS_PATH_FILE,
				"League game.cfg (game.cfg)", nullptr);
	obs_properties_add_int(general, frame_left_key, obs_module_text("LoLPerformanceDashboard.Left"), -32768, 32767,
			       1);
	obs_properties_add_int(general, frame_top_key, obs_module_text("LoLPerformanceDashboard.Top"), -32768, 32767,
			       1);
	obs_properties_add_bool(general, auto_open_key, obs_module_text("LoLGameReport.AutoOpen"));
	obs_properties_add_bool(general, development_logs_key, obs_module_text("LoLGameReport.DevelopmentLogs"));
	obs_properties_add_text(general, "lol_game_report.development_logs_warning",
				obs_module_text("LoLGameReport.DevelopmentLogsWarning"), OBS_TEXT_INFO);
	const QString development_log_path = data ? static_cast<game_report_source *>(data)->development_log_path()
						  : QString();
	obs_properties_add_text(general, "lol_game_report.development_logs_status",
				QString("%1: %2")
					.arg(obs_module_text("LoLGameReport.DevelopmentLogsStatus"),
					     development_log_path.isEmpty()
						     ? obs_module_text("LoLGameReport.DevelopmentLogsNone")
						     : development_log_path)
					.toUtf8()
					.constData(),
				OBS_TEXT_INFO);
	const QString local_url = data ? static_cast<game_report_source *>(data)->recap_url() : QString();
	obs_properties_add_text(
		general, "lol_game_report.local_url",
		QString("%1: %2").arg(obs_module_text("LoLGameReport.LocalURL"), local_url).toUtf8().constData(),
		OBS_TEXT_INFO);
	obs_properties_add_group(props, "lol_game_report.report", obs_module_text("LoLGameReport.Report"),
				 OBS_GROUP_NORMAL, general);
	auto *actions = obs_properties_create();
	obs_properties_add_path(actions, directory_key, obs_module_text("LoLGameReport.ExportDirectory"),
				OBS_PATH_DIRECTORY, nullptr, nullptr);
	obs_properties_add_button2(actions, "lol_game_report.export", obs_module_text("LoLGameReport.Export"),
				   export_clicked, data);
	obs_properties_add_button2(actions, "lol_game_report.delete", obs_module_text("LoLGameReport.Delete"),
				   delete_clicked, data);
	obs_properties_add_button2(actions, "lol_game_report.enrich", obs_module_text("LoLGameReport.Enrich"),
				   enrich_clicked, data);
	auto *reveal = obs_properties_add_button2(actions, "lol_game_report.reveal_development_log",
						  obs_module_text("LoLGameReport.RevealDevelopmentLog"),
						  reveal_development_log_clicked, data);
	obs_property_set_enabled(reveal, data && !development_log_path.isEmpty());
	obs_properties_add_text(actions, "lol_game_report.riot_status",
				QString("%1: %2")
					.arg(obs_module_text("LoLGameReport.RiotStatus"),
					     data ? static_cast<game_report_source *>(data)->riot_status() : "")
					.toUtf8()
					.constData(),
				OBS_TEXT_INFO);
	obs_properties_add_group(props, "lol_game_report.actions", obs_module_text("Preferences.Actions"),
				 OBS_GROUP_NORMAL, actions);
	obs_property_set_modified_callback(latest, latest_changed);
	obs_property_set_visible(list, !data || !static_cast<game_report_source *>(data)->show_latest());
	return props;
}
void defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, latest_key, true);
	obs_data_set_default_string(settings, page_key, "summary");
	obs_data_set_default_string(settings, series_key, "level");
	obs_data_set_default_string(settings, events_key, "kill,objective,tower,level");
	obs_data_set_default_int(settings, dpi_key, 800);
	obs_data_set_default_int(settings, hex_radius_key, lol_game_report::default_hex_radius_percent);
	obs_data_set_default_int(settings, frame_left_key, 0);
	obs_data_set_default_int(settings, frame_top_key, 0);
	obs_data_set_default_bool(settings, auto_open_key, true);
	obs_data_set_default_bool(settings, development_logs_key, false);
	obs_data_set_default_string(
		settings, directory_key,
		QStandardPaths::writableLocation(QStandardPaths::PicturesLocation).toUtf8().constData());
}
} // namespace
void register_lol_game_report_source()
{
	obs_source_info info{};
	info.id = source_id;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = [](void *) {
		return obs_module_text("LoLGameReport");
	};
	info.create = [](obs_data_t *settings, obs_source_t *source) {
		return static_cast<void *>(new game_report_source(source, settings));
	};
	info.destroy = [](void *data) {
		delete static_cast<game_report_source *>(data);
	};
	info.update = [](void *data, obs_data_t *settings) {
		static_cast<game_report_source *>(data)->update(settings);
	};
	info.video_render = [](void *data, gs_effect_t *effect) {
		static_cast<game_report_source *>(data)->draw(effect);
	};
	info.video_tick = [](void *data, float) {
		static_cast<game_report_source *>(data)->tick();
	};
	info.get_width = [](void *) {
		return 1920U;
	};
	info.get_height = [](void *) {
		return 1080U;
	};
	info.get_properties = properties;
	info.get_defaults = defaults;
	obs_register_source(&info);
}
