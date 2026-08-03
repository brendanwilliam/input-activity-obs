#include "lol_game_report_collector.hpp"

#include "lol_game_report_store.hpp"
#include "lol_game_report_web.hpp"

#include "../hook/uiohook_helper.hpp"
#include "../input/input_broker.hpp"

#include <QDateTime>
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
	}
	void set_dpi(int dpi)
	{
		if (active_)
			return;
		pending_dpi_ = std::clamp(dpi, 100, 32000);
	}
	void set_auto_open(bool enabled) { auto_open_ = enabled; }
	QString recap_url() { return web_.url(QString()); }
	void consume_input(const std::vector<input_data::trace_event> &events)
	{
		if (!active_ || events.empty())
			return;
		const int seconds = last_game_seconds_;
		if (report_.input_samples.isEmpty() || report_.input_samples.last().seconds != seconds)
			report_.input_samples.append({seconds});
		auto &sample = report_.input_samples.last();
		for (const auto &event : events) {
			++sample.actions;
			if (event.type == EVENT_MOUSE_MOVED || event.type == EVENT_MOUSE_DRAGGED) {
				if (has_mouse_) {
					const double dx = event.x - last_mouse_x_, dy = event.y - last_mouse_y_;
					sample.mouse_distance_pixels += std::sqrt(dx * dx + dy * dy);
				}
				last_mouse_x_ = event.x;
				last_mouse_y_ = event.y;
				has_mouse_ = true;
				const int x = std::clamp(int(event.x) / 64, 0, 29),
					  y = std::clamp(int(event.y) / 64, 0, 16);
				auto bin = std::find_if(report_.heatmap.begin(), report_.heatmap.end(),
							[x, y](const auto &value) {
								return value.x == x && value.y == y;
							});
				if (bin == report_.heatmap.end())
					report_.heatmap.append({x, y, 1});
				else
					++bin->count;
			}
		}
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
		QObject::connect(reply, &QNetworkReply::finished, reply, [reply, done = std::move(done)] {
			const auto object = reply->error() == QNetworkReply::NoError
						    ? QJsonDocument::fromJson(reply->readAll()).object()
						    : QJsonObject{};
			done(object);
			reply->deleteLater();
		});
	}
	void poll()
	{
		if (pending_)
			return;
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
			if (active_ && ++misses_ >= 3)
				finalize();
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
			player_aliases_ = {riot_id, game_name, name.value("summonerName").toString()};
			state_ = collection_state::recording;
		}
		const QJsonObject game = batch_.value("gamestats").toObject();
		report_.game_mode = game.value("gameMode").toString();
		report_.map = game.value("mapName").toString();
		report_.duration_seconds = game.value("gameTime").toInt();
		last_game_seconds_ = report_.duration_seconds;
		const QJsonObject scores = batch_.value("activeplayerscores").toObject();
		stat_sample sample;
		sample.seconds = game.value("gameTime").toInt();
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
				finalize();
		}
	}
	bool is_local_player(const QString &name) const
	{
		return std::any_of(player_aliases_.cbegin(), player_aliases_.cend(), [&name](const QString &alias) {
			return !alias.isEmpty() && alias.compare(name, Qt::CaseInsensitive) == 0;
		});
	}
	void finalize()
	{
		if (!active_ || finalizing_)
			return;
		finalizing_ = true;
		state_ = collection_state::finalizing;
		report_.completed_at = QDateTime::currentDateTimeUtc();
		report_.chapters = make_chapters(report_.samples, report_.events);
		store().save(report_);
		if (auto_open_)
			web_.open(report_);
		active_ = false;
		finalizing_ = false;
		seen_.clear();
		last_items_.clear();
		ability_levels_.clear();
		has_mouse_ = false;
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
	int pending_dpi_{800}, last_game_seconds_{};
	int16_t last_mouse_x_{}, last_mouse_y_{};
	bool has_mouse_{};
	bool auto_open_{true};
	web_server web_{this};
};

struct shared_collector {
	std::mutex mutex;
	int references{};
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
void collector::tick(int dpi)
{
	set_dpi(dpi);
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
