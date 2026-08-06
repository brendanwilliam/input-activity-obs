#include "sources/game_report/presentation/lol_report_manager.hpp"

#include "sources/game_report/collection/lol_collector.hpp"
#include "sources/game_report/data/lol_store.hpp"
#include "sources/game_report/integration/lol_riot_api.hpp"
#include "sources/game_report/integration/lol_online_reports.hpp"

#include <QProcess>
#include <QStandardPaths>
#include <optional>
#include <obs-module.h>

namespace sources {
namespace {
constexpr const char *latest_key = "lol_dashboard.report.show_latest";
constexpr const char *selected_key = "lol_dashboard.report.selected";
constexpr const char *directory_key = "lol_dashboard.report.export_directory";
constexpr const char *dpi_key = "lol_dashboard.report.mouse_dpi";
constexpr const char *auto_open_key = "lol_dashboard.report.auto_open";
constexpr const char *development_logs_key = "lol_dashboard.report.development_logs";
constexpr const char *online_service_url_key = "lol_dashboard.report.online_service_url";
} // namespace

class lol_report_manager::implementation {
public:
	implementation()
	{
		riot.set_diagnostics([this](const QJsonObject &fields) { collector.log_riot_diagnostic(fields); });
	}
	~implementation()
	{
		if (owner == this)
			owner = nullptr;
	}
	void update(obs_data_t *settings)
	{
		show_latest = obs_data_get_bool(settings, latest_key);
		selected = QString::fromUtf8(obs_data_get_string(settings, selected_key));
		export_directory = QString::fromUtf8(obs_data_get_string(settings, directory_key));
		dpi = int(obs_data_get_int(settings, dpi_key));
		auto_open = obs_data_get_bool(settings, auto_open_key);
		development_logs = obs_data_get_bool(settings, development_logs_key);
		online.set_service_url(QString::fromUtf8(obs_data_get_string(settings, online_service_url_key)));
	}
	void tick(const QRect &game_frame, double hex_radius_percent)
	{
		if (owner && owner != this)
			return;
		owner = this;
		collector.set_dpi(dpi);
		collector.set_hex_radius_percent(hex_radius_percent);
		collector.set_game_frame(game_frame);
		collector.set_auto_open(auto_open);
		collector.set_development_logs(development_logs);
		collector.tick(dpi, hex_radius_percent);
		online.observe(store.reports());
		online.tick();
	}
	std::optional<lol_game_report::report> selected_report() const
	{
		const auto reports = store.reports();
		if (reports.isEmpty())
			return std::nullopt;
		if (show_latest)
			return reports.first();
		for (const auto &report : reports)
			if (report.id == selected)
				return report;
		return std::nullopt;
	}
	lol_game_report::collector collector;
	lol_game_report::store store;
	lol_game_report::riot_api riot;
	lol_game_report::online_reports online;
	bool show_latest{true}, auto_open{true}, development_logs{};
	int dpi{800};
	QString selected, export_directory, riot_status{"Riot enrichment has not run."};
	static implementation *owner;
};

lol_report_manager::implementation *lol_report_manager::implementation::owner{};

lol_report_manager::lol_report_manager() : implementation_(new implementation) {}
lol_report_manager::~lol_report_manager()
{
	delete implementation_;
}
void lol_report_manager::update(obs_data *settings)
{
	implementation_->update(reinterpret_cast<obs_data_t *>(settings));
}
void lol_report_manager::tick(const QRect &game_frame, double radius)
{
	implementation_->tick(game_frame, radius);
}
bool lol_report_manager::export_selected()
{
	const auto report = implementation_->selected_report();
	return report && implementation_->store.export_report(*report, implementation_->export_directory);
}
bool lol_report_manager::delete_selected()
{
	const auto report = implementation_->selected_report();
	return report && implementation_->store.remove(report->id);
}
bool lol_report_manager::enrich_selected()
{
	const auto report = implementation_->selected_report();
	if (!report) {
		implementation_->riot_status = "Select a saved report before enriching.";
		return false;
	}
	implementation_->riot_status = "Requesting Riot Match-v5 data…";
	implementation_->riot.enrich(*report, [this](lol_game_report::report value, const QString &status) {
		if (status.startsWith("Riot Match-v5 enrichment complete"))
			implementation_->store.save(std::move(value));
		implementation_->riot_status = status;
	});
	return true;
}
bool lol_report_manager::reveal_development_log() const
{
	const QString path = implementation_->collector.development_log_path();
	return !path.isEmpty() && QProcess::startDetached("open", {"-R", path});
}
bool lol_report_manager::link_online_reports()
{
	implementation_->online.begin_link();
	return true;
}
bool lol_report_manager::unlink_online_reports()
{
	implementation_->online.unlink();
	return true;
}
bool lol_report_manager::retry_online_reports()
{
	implementation_->online.retry();
	return true;
}
void lol_report_manager::defaults(obs_data *settings)
{
	auto *value = reinterpret_cast<obs_data_t *>(settings);
	obs_data_set_default_bool(value, latest_key, true);
	obs_data_set_default_int(value, dpi_key, 800);
	obs_data_set_default_bool(value, auto_open_key, true);
	obs_data_set_default_bool(value, development_logs_key, false);
	obs_data_set_default_string(value, online_service_url_key, "http://127.0.0.1:3000");
	obs_data_set_default_string(
		value, directory_key,
		QStandardPaths::writableLocation(QStandardPaths::PicturesLocation).toUtf8().constData());
}
void lol_report_manager::add_properties(obs_properties *properties)
{
	auto *props = reinterpret_cast<obs_properties_t *>(properties);
	auto *general = obs_properties_create();
	auto *latest = obs_properties_add_bool(general, latest_key, obs_module_text("LoLGameReport.ShowLatest"));
	auto *list = obs_properties_add_list(general, selected_key, obs_module_text("LoLGameReport.Selected"),
					     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const auto &report : implementation_->store.reports())
		obs_property_list_add_string(list, lol_game_report::display_name(report).toUtf8().constData(),
					     report.id.toUtf8().constData());
	const QString status =
		QString("%1: %2").arg(obs_module_text("LoLGameReport.CollectorStatus"),
				      lol_game_report::collector::state_text(implementation_->collector.state()));
	obs_properties_add_text(general, "lol_dashboard.report.collector_status", status.toUtf8().constData(),
				OBS_TEXT_INFO);
	obs_properties_add_int(general, dpi_key, obs_module_text("LoLGameReport.MouseDPI"), 100, 32000, 50);
	obs_properties_add_bool(general, auto_open_key, obs_module_text("LoLGameReport.AutoOpen"));
	obs_properties_add_bool(general, development_logs_key, obs_module_text("LoLGameReport.DevelopmentLogs"));
	obs_properties_add_text(general, online_service_url_key, obs_module_text("LoLGameReport.OnlineServiceURL"),
				OBS_TEXT_DEFAULT);
	obs_properties_add_text(general, "lol_dashboard.report.local_url",
				QString("%1: %2")
					.arg(obs_module_text("LoLGameReport.LocalURL"),
					     implementation_->collector.recap_url())
					.toUtf8()
					.constData(),
				OBS_TEXT_INFO);
	obs_properties_add_group(props, "lol_dashboard.report", obs_module_text("LoLGameReport.Report"),
				 OBS_GROUP_NORMAL, general);
	auto *actions = obs_properties_create();
	obs_properties_add_path(actions, directory_key, obs_module_text("LoLGameReport.ExportDirectory"),
				OBS_PATH_DIRECTORY, nullptr, nullptr);
	obs_properties_add_button2(
		actions, "lol_dashboard.report.export", obs_module_text("LoLGameReport.Export"),
		[](obs_properties_t *, obs_property_t *, void *data) {
			return static_cast<lol_report_manager *>(data)->export_selected();
		},
		this);
	obs_properties_add_button2(
		actions, "lol_dashboard.report.delete", obs_module_text("LoLGameReport.Delete"),
		[](obs_properties_t *, obs_property_t *, void *data) {
			return static_cast<lol_report_manager *>(data)->delete_selected();
		},
		this);
	obs_properties_add_button2(
		actions, "lol_dashboard.report.enrich", obs_module_text("LoLGameReport.Enrich"),
		[](obs_properties_t *, obs_property_t *, void *data) {
			return static_cast<lol_report_manager *>(data)->enrich_selected();
		},
		this);
	obs_properties_add_button2(
		actions, "lol_dashboard.report.reveal_log", obs_module_text("LoLGameReport.RevealDevelopmentLog"),
		[](obs_properties_t *, obs_property_t *, void *data) {
			return static_cast<lol_report_manager *>(data)->reveal_development_log();
		},
		this);
	obs_properties_add_text(actions, "lol_dashboard.report.riot_status",
				QString("%1: %2")
					.arg(obs_module_text("LoLGameReport.RiotStatus"), implementation_->riot_status)
					.toUtf8()
					.constData(),
				OBS_TEXT_INFO);
	obs_properties_add_group(props, "lol_dashboard.report.actions", obs_module_text("Preferences.Actions"),
				 OBS_GROUP_NORMAL, actions);
	auto *online = obs_properties_create();
	obs_properties_add_text(online, "lol_dashboard.report.online_status",
				QString("%1: %2")
					.arg(obs_module_text("LoLGameReport.OnlineStatus"),
					     implementation_->online.status())
					.toUtf8()
					.constData(),
				OBS_TEXT_INFO);
	obs_properties_add_button2(
		online, "lol_dashboard.report.online_link", obs_module_text("LoLGameReport.OnlineLink"),
		[](obs_properties_t *, obs_property_t *, void *data) {
			return static_cast<lol_report_manager *>(data)->link_online_reports();
		},
		this);
	obs_properties_add_button2(
		online, "lol_dashboard.report.online_unlink", obs_module_text("LoLGameReport.OnlineUnlink"),
		[](obs_properties_t *, obs_property_t *, void *data) {
			return static_cast<lol_report_manager *>(data)->unlink_online_reports();
		},
		this);
	obs_properties_add_button2(
		online, "lol_dashboard.report.online_retry", obs_module_text("LoLGameReport.OnlineRetry"),
		[](obs_properties_t *, obs_property_t *, void *data) {
			return static_cast<lol_report_manager *>(data)->retry_online_reports();
		},
		this);
	obs_properties_add_group(props, "lol_dashboard.report.online", obs_module_text("LoLGameReport.Online"),
				 OBS_GROUP_NORMAL, online);
	obs_property_set_modified_callback(latest, [](obs_properties_t *all, obs_property_t *, obs_data_t *settings) {
		obs_property_set_visible(obs_properties_get(all, selected_key),
					 !obs_data_get_bool(settings, latest_key));
		return true;
	});
	obs_property_set_visible(list, !implementation_->show_latest);
}
} // namespace sources
