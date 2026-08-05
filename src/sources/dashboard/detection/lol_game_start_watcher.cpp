#include "sources/dashboard/detection/lol_game_start_watcher.hpp"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPluginLoader>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <memory>

#include <obs-module.h>

extern "C" {
#include <util/bmem.h>
}

namespace sources {
namespace {
class worker final : public QObject {
public:
	explicit worker(std::atomic<uint64_t> &starts) : starts_(starts) {}

	void start()
	{
		load_tls_backend();
		manager_ = new QNetworkAccessManager(this);
		timer_ = new QTimer(this);
		QObject::connect(timer_, &QTimer::timeout, this, [this] { poll(); });
		timer_->start(1000);
		poll();
	}
	void stop()
	{
		if (timer_)
			timer_->stop();
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
	void poll()
	{
		if (pending_)
			return;
		pending_ = true;
		QNetworkRequest request(QUrl("https://127.0.0.1:2999/liveclientdata/gamestats"));
		request.setTransferTimeout(800);
		QSslConfiguration ssl = request.sslConfiguration();
		ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
		request.setSslConfiguration(ssl);
		auto *reply = manager_->get(request);
		reply->ignoreSslErrors();
		QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply] {
			pending_ = false;
			const bool success = reply->error() == QNetworkReply::NoError;
			const double game_time =
				success ? QJsonDocument::fromJson(reply->readAll()).object()["gameTime"].toDouble()
					: 0.0;
			if (success)
				failures_ = 0;
			else
				++failures_;
			const bool game_active = success ? game_time > 0.0 : failures_ < 3 && active_;
			const uint64_t starts = detector_.observe(game_active);
			starts_.store(starts, std::memory_order_release);
			active_ = game_active;
			reply->deleteLater();
		});
	}

	std::atomic<uint64_t> &starts_;
	QNetworkAccessManager *manager_{};
	QTimer *timer_{};
	std::unique_ptr<QPluginLoader> tls_backend_;
	lol_game_start_detector detector_;
	bool pending_{};
	bool active_{};
	int failures_{};
};
} // namespace

struct lol_dashboard_game_start_watcher::implementation {
	std::atomic<uint64_t> starts{};
	QThread thread;
	worker *worker_{};

	implementation()
	{
		worker_ = new worker(starts);
		worker_->moveToThread(&thread);
		QObject::connect(&thread, &QThread::started, worker_, [this] { worker_->start(); });
		thread.start();
	}
	~implementation()
	{
		QMetaObject::invokeMethod(worker_, [this] { worker_->stop(); }, Qt::BlockingQueuedConnection);
		thread.quit();
		thread.wait();
		delete worker_;
	}
};

lol_dashboard_game_start_watcher::lol_dashboard_game_start_watcher() : implementation_(new implementation) {}
lol_dashboard_game_start_watcher::~lol_dashboard_game_start_watcher() = default;

bool lol_dashboard_game_start_watcher::consume_start(uint64_t &cursor) const
{
	const uint64_t latest = implementation_->starts.load(std::memory_order_acquire);
	if (latest <= cursor)
		return false;
	cursor = latest;
	return true;
}

} // namespace sources
