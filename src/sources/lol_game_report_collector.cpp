#include "lol_game_report_collector.hpp"

#include "lol_game_report_diagnostics.hpp"
#include "lol_report_input_telemetry.hpp"
#include "lol_game_report_riot_api.hpp"
#include "lol_game_report_store.hpp"
#include "lol_game_report_web.hpp"

#include "../hook/uiohook_helper.hpp"
#include "../input/input_broker.hpp"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPluginLoader>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslSocket>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QSet>
#include <QStringList>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <numeric>
#include <vector>
#include <obs-module.h>

extern "C" {
#include <util/bmem.h>
}

namespace sources::lol_game_report {
namespace {
class worker final : public QObject {
public:
	explicit worker(std::atomic<collection_state> &state) : state_(state) {}
	void start()
	{
		load_tls_backend();
		manager_ = new QNetworkAccessManager(this);
		timer_ = new QTimer(this);
		QObject::connect(timer_, &QTimer::timeout, this, [this] { poll(); });
		timer_->start(2000);
		poll();
	}
	void stop()
	{
		if (timer_)
			timer_->stop();
		diagnostics_.write("collector", "worker_stopped");
		diagnostics_.close_and_remove();
	}
	void set_dpi(int dpi)
	{
		if (active_)
			return;
		pending_dpi_ = std::clamp(dpi, 100, 32000);
	}
	void set_hex_radius_percent(int radius_percent)
	{
		if (!active_)
			pending_hex_radius_percent_ = std::clamp(radius_percent, 1, 20);
	}
	void set_auto_open(bool enabled) { auto_open_ = enabled; }
	void set_development_logs(bool enabled)
	{
		diagnostics_.set_enabled(enabled);
		diagnostics_.write("collector", "development_logs_changed", {{"enabled", enabled}});
	}
	bool development_logs_enabled() const { return diagnostics_.enabled(); }
	QString development_log_path() const { return diagnostics_.path(); }
	void log_riot_diagnostic(QJsonObject fields) { diagnostics_.write("riot_enrichment", "request", fields); }
	QString recap_url() { return web_.url(QString()); }
	void set_game_frame(const QRect &frame)
	{
		if (!active_)
			pending_game_frame_ = frame;
	}
	void consume_input(const std::vector<input_data::trace_event> &events)
	{
		if (!active_ || events.empty())
			return;
		const int seconds = last_game_seconds_;
		int movement_count{};
		for (const auto &event : events)
			movement_count += event.type == EVENT_MOUSE_MOVED || event.type == EVENT_MOUSE_DRAGGED;
		const double before_distance =
			report_.input_samples.isEmpty()
				? 0.0
				: std::accumulate(report_.input_samples.cbegin(), report_.input_samples.cend(), 0.0,
						  [](double total, const auto &sample) {
							  return total + sample.mouse_distance_pixels;
						  });
		telemetry_.consume(events, seconds, report_.input_samples, report_.hexbins);
		const double after_distance = std::accumulate(
			report_.input_samples.cbegin(), report_.input_samples.cend(), 0.0,
			[](double total, const auto &sample) { return total + sample.mouse_distance_pixels; });
		diagnostics_.write("input", seconds <= 0 ? "input_accepted_zero_clock" : "input_accepted",
				   {{"sample_seconds", seconds},
				    {"action_count", int(events.size())},
				    {"movement_count", movement_count},
				    {"distance_pixels", after_distance - before_distance},
				    {"clock_aligned", seconds > 0}});
	}

private:
	void load_tls_backend()
	{
		char *path = obs_module_file("tls/libqsecuretransportbackend.dylib");
		if (!path)
			return;
		tls_backend_ = std::make_unique<QPluginLoader>(QString::fromUtf8(path));
		bfree(path);
		tls_backend_->instance();
	}
	void get(const QString &path, std::function<void(const QJsonObject &)> done)
	{
		auto elapsed = std::make_shared<QElapsedTimer>();
		elapsed->start();
		QNetworkRequest request(QUrl("https://127.0.0.1:2999/liveclientdata/" + path));
		request.setTransferTimeout(1200);
		// This request URL is constructed solely from this module's fixed loopback endpoint and
		// endpoint names. Riot's Live Client Data service uses a self-signed certificate there.
		QSslConfiguration ssl = request.sslConfiguration();
		ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
		request.setSslConfiguration(ssl);
		auto *reply = manager_->get(request);
		reply->ignoreSslErrors();
		QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &) {
			// Riot's self-signed local certificate is accepted only for this literal loopback request.
			if (reply->url().host() == "127.0.0.1" && reply->url().port(2999) == 2999)
				reply->ignoreSslErrors();
		});
		QObject::connect(
			reply, &QNetworkReply::finished, reply, [this, reply, path, elapsed, done = std::move(done)] {
				const bool success = reply->error() == QNetworkReply::NoError;
				const QByteArray body = reply->readAll();
				const QJsonDocument document = success ? QJsonDocument::fromJson(body)
								       : QJsonDocument{};
				const QJsonObject object = document.object();
				const QJsonObject playerlist =
					document.isArray() ? QJsonObject{{"allPlayers", document.array()}} : object;
				QJsonObject fields{{"endpoint", path},
						   {"success", success},
						   {"http_status",
						    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()},
						   {"latency_ms", int(elapsed->elapsed())}};
				if (success) {
					if (path == "eventdata")
						fields.insert("payload", sanitize_eventdata(object));
					else if (path == "playerlist")
						fields.insert("payload", summarize_playerlist(playerlist));
					else if (path != "playerlist")
						fields.insert("payload", object);
				}
				diagnostics_.write("collector", "endpoint_completed", fields);
				done(object);
				reply->deleteLater();
			});
	}
	void poll()
	{
		if (pending_)
			return;
		diagnostics_.write("collector", "poll_started");
		pending_ = 7;
		batch_ = {};
		for (const QString &endpoint : {"activeplayer", "activeplayerscores", "activeplayeritems",
						"activeplayerrunes", "eventdata", "gamestats", "playerlist"})
			get(endpoint, [this, endpoint](const QJsonObject &object) {
				batch_[endpoint] = object;
				if (--pending_ == 0)
					process_batch();
			});
	}
	void process_batch()
	{
		const QJsonObject name = batch_.value("activeplayer").toObject();
		const QString riot_id = name.value("riotId").toString();
		const QString game_name = name.value("riotIdGameName").toString();
		const QString player = riot_id.isEmpty() ? game_name : riot_id;
		if (player.isEmpty()) {
			diagnostics_.write("collector", "missing_player",
					   {{"active", active_}, {"misses", misses_ + 1}});
			if (active_ && ++misses_ >= 3)
				finalize("missing_player");
			return;
		}
		misses_ = 0;
		if (!active_) {
			active_ = true;
			report_ = {};
			report_.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
			report_.player = player;
			report_.champion = name.value("championName").toString();
			report_.dpi = pending_dpi_;
			report_.hex_geometry.radius_percent = pending_hex_radius_percent_;
			report_.hex_geometry.frame_aspect_ratio =
				double(pending_game_frame_.width()) / std::max(1, pending_game_frame_.height());
			player_aliases_ = {riot_id, game_name, name.value("summonerName").toString()};
			telemetry_.set_game_frame(pending_game_frame_);
			telemetry_.set_hex_radius_percent(report_.hex_geometry.radius_percent);
			telemetry_.reset();
			state_ = collection_state::recording;
			diagnostics_.write("collector", "report_started", {{"has_riot_id", !riot_id.isEmpty()}});
		}
		const QJsonObject game = batch_.value("gamestats").toObject();
		report_.game_mode = game.value("gameMode").toString();
		report_.map = game.value("mapName").toString();
		report_.duration_seconds = int(std::floor(game.value("gameTime").toDouble()));
		last_game_seconds_ = report_.duration_seconds;
		if (last_logged_game_seconds_ != last_game_seconds_) {
			diagnostics_.write("collector", "game_clock_changed",
					   {{"game_seconds", last_game_seconds_},
					    {"clock_aligned", last_game_seconds_ > 0}});
			last_logged_game_seconds_ = last_game_seconds_;
		}
		const QJsonObject scores = batch_.value("activeplayerscores").toObject();
		stat_sample sample;
		sample.seconds = int(std::floor(game.value("gameTime").toDouble()));
		sample.kills = scores.value("kills").toInt();
		sample.deaths = scores.value("deaths").toInt();
		sample.assists = scores.value("assists").toInt();
		sample.cs = scores.value("creepScore").toInt();
		sample.ward_score = scores.value("wardScore").toInt();
		sample.level = scores.value("level").toInt();
		sample.gold = scores.value("currentGold").toInt();
		report_.samples.append(sample);
		report_.items.clear();
		const auto items = batch_.value("activeplayeritems").toObject().value("items").toArray();
		QStringList current_items;
		for (const QJsonValue item : items)
			current_items.append(item.toObject().value("displayName").toString());
		for (const auto &item : current_items)
			if (!last_items_.contains(item) && !item.isEmpty())
				report_.item_events.append({item, 0, sample.seconds});
		report_.items = current_items;
		last_items_ = current_items;
		const auto abilities = name.value("abilities").toObject();
		for (const QString &slot : {"Q", "W", "E", "R"}) {
			const auto ability = abilities.value(slot).toObject();
			const int level = ability.value("abilityLevel").toInt();
			if (level > ability_levels_.value(slot)) {
				report_.abilities.append({slot, level, sample.seconds});
				ability_levels_[slot] = level;
			}
		}
		report_.runes.clear();
		const auto runes = batch_.value("activeplayerrunes").toObject();
		if (!runes.value("keystone").toObject().value("displayName").toString().isEmpty())
			report_.runes.append(runes.value("keystone").toObject().value("displayName").toString());
		const auto events = batch_.value("eventdata").toObject().value("Events").toArray();
		for (const QJsonValue item : events) {
			const auto event = item.toObject();
			const QString id = event.value("EventID").toString();
			if (id.isEmpty() || seen_.contains(id))
				continue;
			const QString type = event.value("EventName").toString();
			const QString killer = event.value("KillerName").toString(),
				      victim = event.value("VictimName").toString();
			if (type != "GameEnd" && !is_local_player(killer) && !is_local_player(victim) &&
			    classify_event(type) == "other")
				continue;
			seen_.insert(id);
			report_.events.append(
				{id, type, int(event.value("EventTime").toDouble()), type, classify_event(type)});
			if (type == "GameEnd")
				finalize("game_end_event");
		}
		diagnostics_.write("collector", "poll_completed",
				   {{"sample_count", report_.samples.size()},
				    {"event_count", report_.events.size()},
				    {"game_seconds", report_.duration_seconds}});
	}
	bool is_local_player(const QString &name) const
	{
		return std::any_of(player_aliases_.cbegin(), player_aliases_.cend(), [&name](const QString &alias) {
			return !alias.isEmpty() && alias.compare(name, Qt::CaseInsensitive) == 0;
		});
	}
	void finalize(const QString &reason)
	{
		if (!active_ || finalizing_)
			return;
		finalizing_ = true;
		state_ = collection_state::finalizing;
		const bool no_valid_samples =
			std::none_of(report_.samples.cbegin(), report_.samples.cend(),
				     [](const stat_sample &sample) { return sample.seconds > 0; });
		if (report_.duration_seconds <= 0 || report_.game_id.isEmpty() || no_valid_samples)
			diagnostics_.write("collector", "report_finalization_warning",
					   {{"reason", reason},
					    {"zero_duration", report_.duration_seconds <= 0},
					    {"absent_game_id", report_.game_id.isEmpty()},
					    {"no_valid_stat_samples", no_valid_samples},
					    {"duration_seconds", report_.duration_seconds},
					    {"sample_count", report_.samples.size()}});
		report_.completed_at = QDateTime::currentDateTimeUtc();
		report_.chapters = make_chapters(report_.samples, report_.events);
		store().save(report_);
		diagnostics_.write("collector", "report_finalized",
				   {{"reason", reason},
				    {"sample_count", report_.samples.size()},
				    {"event_count", report_.events.size()},
				    {"input_sample_count", report_.input_samples.size()}});
		if (auto_open_)
			web_.open(report_);
		riot_.enrich_latest(report_, [this](report value, const QString &status) {
			if (status.startsWith("Riot Match-v5 enrichment complete"))
				store().save(std::move(value));
			diagnostics_.write("riot_enrichment", "automatic_completed", {{"status", status}});
		});
		active_ = false;
		finalizing_ = false;
		seen_.clear();
		last_items_.clear();
		ability_levels_.clear();
		telemetry_.reset();
		last_logged_game_seconds_ = -1;
		state_ = collection_state::empty;
	}
	std::atomic<collection_state> &state_;
	QNetworkAccessManager *manager_{};
	QTimer *timer_{};
	std::unique_ptr<QPluginLoader> tls_backend_;
	int pending_{};
	int misses_{};
	bool active_{};
	bool finalizing_{};
	QJsonObject batch_;
	report report_;
	QSet<QString> seen_;
	QStringList player_aliases_;
	QStringList last_items_;
	QHash<QString, int> ability_levels_;
	int pending_dpi_{800}, pending_hex_radius_percent_{default_hex_radius_percent}, last_game_seconds_{};
	QRect pending_game_frame_{0, 0, 1920, 1080};
	input_telemetry telemetry_;
	bool auto_open_{true};
	int last_logged_game_seconds_{-1};
	diagnostic_log diagnostics_;
	web_server web_{this};
	riot_api riot_{this};
};

struct shared_collector {
	std::mutex mutex;
	int references{};
	int development_log_references{};
	std::atomic<collection_state> state{collection_state::empty};
	QThread thread;
	worker *worker{};
	void acquire()
	{
		std::lock_guard lock(mutex);
		if (references++ != 0)
			return;
		worker = new class worker(state);
		worker->moveToThread(&thread);
		QObject::connect(&thread, &QThread::started, worker, [this] { worker->start(); });
		thread.start();
	}
	void release()
	{
		std::lock_guard lock(mutex);
		if (references <= 0)
			return;
		if (--references != 0)
			return;
		QMetaObject::invokeMethod(worker, [this] { worker->stop(); }, Qt::BlockingQueuedConnection);
		thread.quit();
		thread.wait();
		delete worker;
		worker = nullptr;
		state = collection_state::empty;
	}
	void set_dpi(int dpi)
	{
		QMetaObject::invokeMethod(worker, [this, dpi] { worker->set_dpi(dpi); }, Qt::QueuedConnection);
	}
	void set_hex_radius_percent(int radius_percent)
	{
		QMetaObject::invokeMethod(
			worker, [this, radius_percent] { worker->set_hex_radius_percent(radius_percent); },
			Qt::QueuedConnection);
	}
	void set_game_frame(const QRect &frame)
	{
		QMetaObject::invokeMethod(
			worker, [this, frame] { worker->set_game_frame(frame); }, Qt::QueuedConnection);
	}
	void consume_input(const std::vector<input_data::trace_event> &events)
	{
		if (!events.empty())
			QMetaObject::invokeMethod(
				worker, [this, events] { worker->consume_input(events); }, Qt::QueuedConnection);
	}
	void set_auto_open(bool enabled)
	{
		QMetaObject::invokeMethod(
			worker, [this, enabled] { worker->set_auto_open(enabled); }, Qt::QueuedConnection);
	}
	void set_development_logs(bool enabled)
	{
		std::lock_guard lock(mutex);
		if (enabled)
			++development_log_references;
		else if (development_log_references > 0)
			--development_log_references;
		if (!worker)
			return;
		const bool active = development_log_references > 0;
		QMetaObject::invokeMethod(
			worker, [this, active] { worker->set_development_logs(active); }, Qt::QueuedConnection);
	}
	bool development_logs_enabled()
	{
		std::lock_guard lock(mutex);
		return development_log_references > 0;
	}
	QString development_log_path()
	{
		QString result;
		std::lock_guard lock(mutex);
		if (worker)
			QMetaObject::invokeMethod(
				worker, [this, &result] { result = worker->development_log_path(); },
				Qt::BlockingQueuedConnection);
		return result;
	}
	void log_riot_diagnostic(const QJsonObject &fields)
	{
		std::lock_guard lock(mutex);
		if (worker)
			QMetaObject::invokeMethod(
				worker, [this, fields] { worker->log_riot_diagnostic(fields); }, Qt::QueuedConnection);
	}
	QString recap_url()
	{
		QString result;
		QMetaObject::invokeMethod(
			worker, [this, &result] { result = worker->recap_url(); }, Qt::BlockingQueuedConnection);
		return result;
	}
};
shared_collector &shared()
{
	static shared_collector value;
	return value;
}
} // namespace

collector::collector()
{
	shared().acquire();
}
collector::~collector()
{
	if (development_logs_)
		shared().set_development_logs(false);
	shared().release();
}
collection_state collector::state() const
{
	return shared().state.load();
}
void collector::set_dpi(int dpi)
{
	shared().set_dpi(dpi);
}
void collector::set_hex_radius_percent(int radius_percent)
{
	shared().set_hex_radius_percent(radius_percent);
}
void collector::set_game_frame(const QRect &frame)
{
	shared().set_game_frame(frame);
}
void collector::tick(int dpi, int hex_radius_percent)
{
	set_dpi(dpi);
	set_hex_radius_percent(hex_radius_percent);
	if (!uiohook::league_game_is_frontmost()) {
		discard_backlog_ = true;
		return;
	}
	std::vector<input_data::trace_event> events;
	input_data::button_map<uint16_t> keyboard, mouse;
	input_broker::consume({}, cursor_, discard_backlog_, events, keyboard, mouse);
	// The broker carries key characters for live-key displays. Reports retain only action counts.
	for (auto &event : events)
		event.keychar = 0;
	shared().consume_input(events);
}
void collector::set_auto_open(bool enabled)
{
	shared().set_auto_open(enabled);
}
void collector::set_development_logs(bool enabled)
{
	if (development_logs_ == enabled)
		return;
	development_logs_ = enabled;
	shared().set_development_logs(enabled);
}
bool collector::development_logs_enabled() const
{
	return shared().development_logs_enabled();
}
QString collector::development_log_path() const
{
	return shared().development_log_path();
}
void collector::log_riot_diagnostic(const QJsonObject &fields)
{
	shared().log_riot_diagnostic(fields);
}
QString collector::recap_url() const
{
	return shared().recap_url();
}
QString collector::state_text(collection_state value)
{
	if (value == collection_state::recording)
		return "Recording self-only local game data";
	if (value == collection_state::finalizing)
		return "Finalizing report";
	return "Waiting for a local League game";
}
} // namespace sources::lol_game_report
