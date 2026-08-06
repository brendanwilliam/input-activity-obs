#include "sources/game_report/presentation/lol_report_manager.hpp"

#include "sources/game_report/collection/lol_collector.hpp"
#include "sources/game_report/integration/lol_online_reports.hpp"

#include <QPointer>
#include <QProcess>
#include <obs-module.h>

namespace sources {
namespace {
constexpr const char *dpi_key = "lol_dashboard.report.mouse_dpi";
constexpr const char *development_logs_key = "lol_dashboard.report.development_logs";
constexpr const char *online_service_url_key = "lol_dashboard.report.online_service_url";
} // namespace

class lol_report_manager::implementation {
public:
	implementation()
	{
		const QPointer<lol_game_report::online_reports> online_reports = &online;
		collector.set_submission_callback([online_reports](const lol_game_report::report &report) {
			if (!online_reports)
				return;
			QMetaObject::invokeMethod(
				online_reports,
				[online_reports, report] {
					if (online_reports)
						online_reports->submit(report);
				},
				Qt::QueuedConnection);
		});
	}
	~implementation()
	{
		collector.set_submission_callback({});
		if (owner == this)
			owner = nullptr;
	}
	void update(obs_data_t *settings)
	{
		dpi = int(obs_data_get_int(settings, dpi_key));
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
		collector.set_development_logs(development_logs);
		collector.tick(dpi, hex_radius_percent);
	}
	lol_game_report::collector collector;
	lol_game_report::online_reports online;
	bool development_logs{};
	int dpi{800};
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
bool lol_report_manager::reveal_development_log() const
{
	const QString path = implementation_->collector.development_log_path();
	return !path.isEmpty() && QProcess::startDetached("open", {"-R", path});
}
bool lol_report_manager::link_online_reports()
{
	auto &online = implementation_->online;
	QMetaObject::invokeMethod(&online, [&online] { online.begin_link(); }, Qt::QueuedConnection);
	return true;
}
bool lol_report_manager::unlink_online_reports()
{
	auto &online = implementation_->online;
	QMetaObject::invokeMethod(&online, [&online] { online.unlink(); }, Qt::QueuedConnection);
	return true;
}
bool lol_report_manager::retry_online_reports()
{
	auto &online = implementation_->online;
	QMetaObject::invokeMethod(&online, [&online] { online.retry(); }, Qt::QueuedConnection);
	return true;
}
void lol_report_manager::defaults(obs_data *settings)
{
	auto *value = reinterpret_cast<obs_data_t *>(settings);
	obs_data_set_default_int(value, dpi_key, 800);
	obs_data_set_default_bool(value, development_logs_key, false);
	obs_data_set_default_string(value, online_service_url_key, ONLINE_REPORTS_SERVICE_URL);
}
void lol_report_manager::add_properties(obs_properties *properties)
{
	auto *props = reinterpret_cast<obs_properties_t *>(properties);
	auto *general = obs_properties_create();
	const QString status =
		QString("%1: %2").arg(obs_module_text("LoLGameReport.CollectorStatus"),
				      lol_game_report::collector::state_text(implementation_->collector.state()));
	obs_properties_add_text(general, "lol_dashboard.report.collector_status", status.toUtf8().constData(),
				OBS_TEXT_INFO);
	obs_properties_add_int(general, dpi_key, obs_module_text("LoLGameReport.MouseDPI"), 100, 32000, 50);
	obs_properties_add_bool(general, development_logs_key, obs_module_text("LoLGameReport.DevelopmentLogs"));
	obs_properties_add_text(general, online_service_url_key, obs_module_text("LoLGameReport.OnlineServiceURL"),
				OBS_TEXT_DEFAULT);
	obs_properties_add_group(props, "lol_dashboard.report", obs_module_text("LoLGameReport.Report"),
				 OBS_GROUP_NORMAL, general);
	auto *actions = obs_properties_create();
	obs_properties_add_button2(
		actions, "lol_dashboard.report.reveal_log", obs_module_text("LoLGameReport.RevealDevelopmentLog"),
		[](obs_properties_t *, obs_property_t *, void *data) {
			return static_cast<lol_report_manager *>(data)->reveal_development_log();
		},
		this);
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
}
} // namespace sources
