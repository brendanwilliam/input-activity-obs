#include "sources/game_report/integration/lol_online_reports.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace sources::lol_game_report {
namespace {
constexpr auto service_name = "com.brendanwilliam.input-activity.online-reports";
constexpr auto account_name = "reports:write";
#ifndef ONLINE_REPORTS_SERVICE_URL
#define ONLINE_REPORTS_SERVICE_URL "https://handscheck.vercel.app"
#endif

struct pending_report {
	QJsonObject payload;
	QDateTime retry_at;
	int attempts{};
};

QString payload_hash(const QJsonObject &value)
{
	return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(value).toJson(QJsonDocument::Compact),
							    QCryptographicHash::Sha256)
					   .toHex());
}
} // namespace

class online_reports::implementation {
public:
	explicit implementation(online_reports *owner) : owner(owner), network(owner)
	{
		root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/league-game-reports";
		QDir().mkpath(root);
		device_poll_timer.setInterval(1000);
		QObject::connect(&device_poll_timer, &QTimer::timeout, owner, [owner] { owner->poll_device_code(); });
		upload_timer.setInterval(1000);
		QObject::connect(&upload_timer, &QTimer::timeout, owner, [owner] { owner->tick(); });
		upload_timer.start();
	}
	online_reports *owner;
	QNetworkAccessManager network;
	QVector<pending_report> queue;
	QHash<QString, QString> uploaded_payloads;
	QString root, state{"Not linked. Online reports are disabled."}, device_code, token;
	QUrl service_url{ONLINE_REPORTS_SERVICE_URL};
	QDateTime device_code_expires_at, next_device_poll;
	QTimer device_poll_timer, upload_timer;
	bool auth_required{}, upload_in_flight{};
};

online_reports::online_reports(QObject *parent) : QObject(parent), implementation_(new implementation(this))
{
	load_queue();
	implementation_->token = credential();
}

online_reports::~online_reports()
{
	delete implementation_;
}

void online_reports::load_queue()
{
	QFile file(implementation_->root + "/online-upload-queue.json");
	if (!file.open(QIODevice::ReadOnly))
		return;
	const QJsonObject saved = QJsonDocument::fromJson(file.readAll()).object();
	const QJsonArray entries = saved["queue"].toArray();
	for (const auto entry : entries) {
		const auto object = entry.toObject();
		implementation_->queue.append({object["payload"].toObject(),
					       QDateTime::fromString(object["retry_at"].toString(), Qt::ISODateWithMs),
					       object["attempts"].toInt()});
	}
	for (const auto entry : saved["uploaded"].toArray()) {
		const QJsonObject value = entry.toObject();
		const QString id = value["id"].toString(), hash = value["payload_hash"].toString();
		if (!id.isEmpty() && !hash.isEmpty())
			implementation_->uploaded_payloads.insert(id, hash);
	}
}

void online_reports::save_queue() const
{
	QJsonArray values;
	for (const auto &entry : implementation_->queue)
		values.append(QJsonObject{{"payload", entry.payload},
					  {"retry_at", entry.retry_at.toUTC().toString(Qt::ISODateWithMs)},
					  {"attempts", entry.attempts}});
	QJsonArray uploaded;
	for (auto entry = implementation_->uploaded_payloads.cbegin();
	     entry != implementation_->uploaded_payloads.cend(); ++entry)
		uploaded.append(QJsonObject{{"id", entry.key()}, {"payload_hash", entry.value()}});
	QSaveFile file(implementation_->root + "/online-upload-queue.json");
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(QJsonObject{{"schema_version", 2}, {"queue", values}, {"uploaded", uploaded}})
				   .toJson(QJsonDocument::Compact));
		file.commit();
	}
}

QString online_reports::credential() const
{
	QProcess process;
	process.start("/usr/bin/security", {"find-generic-password", "-w", "-a", account_name, "-s", service_name});
	if (!process.waitForFinished(1000) || process.exitCode() != 0)
		return {};
	return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

bool online_reports::save_credential(const QString &value) const
{
	QProcess process;
	process.start("/usr/bin/security",
		      {"add-generic-password", "-U", "-a", account_name, "-s", service_name, "-w", value});
	return process.waitForFinished(1000) && process.exitCode() == 0;
}

void online_reports::clear_credential() const
{
	QProcess process;
	process.start("/usr/bin/security", {"delete-generic-password", "-a", account_name, "-s", service_name});
	process.waitForFinished(1000);
}

void online_reports::observe(const QVector<report> &reports)
{
	if (!linked())
		return;
	for (const auto &value : reports) {
		const QJsonObject payload = to_json(value);
		const QString hash = payload_hash(payload);
		if (implementation_->uploaded_payloads.value(value.id) == hash)
			continue;
		bool found{};
		for (auto &entry : implementation_->queue) {
			if (entry.payload["id"] == value.id) {
				found = true;
				if (payload_hash(entry.payload) != hash) {
					entry.payload = payload;
					entry.retry_at = QDateTime::currentDateTimeUtc();
					entry.attempts = 0;
				}
			}
		}
		if (!found)
			implementation_->queue.append({payload, QDateTime::currentDateTimeUtc(), 0});
	}
	save_queue();
}

void online_reports::set_service_url(const QString &value)
{
	QUrl candidate(value.trimmed());
	if (candidate.isValid() && !candidate.scheme().isEmpty() && !candidate.host().isEmpty())
		implementation_->service_url = candidate;
}

void online_reports::tick()
{
	if (!linked() || implementation_->auth_required || implementation_->upload_in_flight ||
	    implementation_->queue.isEmpty())
		return;
	auto &entry = implementation_->queue.first();
	if (entry.retry_at > QDateTime::currentDateTimeUtc())
		return;
	QNetworkRequest request(implementation_->service_url.resolved(QUrl("/api/reports")));
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	request.setRawHeader("Authorization", "Bearer " + credential().toUtf8());
	request.setRawHeader("Idempotency-Key", entry.payload["id"].toString().toUtf8());
	implementation_->upload_in_flight = true;
	auto *reply =
		implementation_->network.post(request, QJsonDocument(entry.payload).toJson(QJsonDocument::Compact));
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		implementation_->upload_in_flight = false;
		const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (!implementation_->queue.isEmpty() && reply->error() == QNetworkReply::NoError && code >= 200 &&
		    code < 300) {
			const QJsonObject uploaded = implementation_->queue.first().payload;
			implementation_->queue.removeFirst();
			implementation_->uploaded_payloads.insert(uploaded["id"].toString(), payload_hash(uploaded));
			implementation_->state = implementation_->queue.isEmpty() ? "Connected. All reports uploaded."
										  : "Connected. Uploading reports.";
		} else if (code == 401 || code == 403) {
			implementation_->auth_required = true;
			implementation_->state =
				"Link required. Your online-reports permission was revoked or expired.";
		} else if (!implementation_->queue.isEmpty()) {
			auto &entry = implementation_->queue.first();
			entry.attempts = qMin(entry.attempts + 1, 8);
			entry.retry_at = QDateTime::currentDateTimeUtc().addSecs(1 << qMin(entry.attempts, 8));
			implementation_->state = "Upload delayed; it will retry automatically.";
		}
		save_queue();
		reply->deleteLater();
	});
}

void online_reports::begin_link()
{
	if (!implementation_->device_code.isEmpty())
		return;
	QNetworkRequest request(implementation_->service_url.resolved(QUrl("/api/device/start")));
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	auto *reply = implementation_->network.post(request, R"({"client_name":"Hands Check"})");
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		const auto value = QJsonDocument::fromJson(reply->readAll()).object();
		implementation_->device_code = value["device_code"].toString();
		QUrl verification(value["verification_uri"].toString());
		const QString code = value["user_code"].toString();
		if (!implementation_->device_code.isEmpty() && verification.isValid() && !code.isEmpty()) {
			implementation_->state =
				QString("Approve online reports in your browser with code %1.").arg(code);
			implementation_->next_device_poll =
				QDateTime::currentDateTimeUtc().addSecs(value["interval"].toInt(5));
			implementation_->device_code_expires_at =
				QDateTime::currentDateTimeUtc().addSecs(value["expires_in"].toInt(600));
			implementation_->device_poll_timer.start();
			QUrlQuery query(verification);
			query.addQueryItem("code", code);
			verification.setQuery(query);
			QMetaObject::invokeMethod(
				QCoreApplication::instance(),
				[verification] { QDesktopServices::openUrl(verification); }, Qt::QueuedConnection);
		} else {
			implementation_->state = "Could not start online linking. Please try again.";
		}
		reply->deleteLater();
	});
}

void online_reports::poll_device_code()
{
	if (implementation_->device_code.isEmpty())
		return;
	const QDateTime now = QDateTime::currentDateTimeUtc();
	if (implementation_->device_code_expires_at.isValid() && implementation_->device_code_expires_at <= now) {
		implementation_->device_code.clear();
		implementation_->device_poll_timer.stop();
		implementation_->state = "Link request expired. Start linking again.";
		return;
	}
	if (implementation_->next_device_poll > now)
		return;
	implementation_->next_device_poll = now.addSecs(5);
	QNetworkRequest request(implementation_->service_url.resolved(QUrl("/api/device/token")));
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	auto *reply = implementation_->network.post(
		request, QJsonDocument(QJsonObject{{"device_code", implementation_->device_code}}).toJson());
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		const QString token = QJsonDocument::fromJson(reply->readAll()).object()["access_token"].toString();
		if (!token.isEmpty() && save_credential(token)) {
			implementation_->token = token;
			implementation_->device_code.clear();
			implementation_->device_code_expires_at = {};
			implementation_->device_poll_timer.stop();
			implementation_->auth_required = false;
			// A relink can point this device at a different account. Re-observe
			// saved reports once in that account, then persist their acknowledgements.
			implementation_->uploaded_payloads.clear();
			save_queue();
			implementation_->state =
				"Connected. New completed reports will upload privately to your profile.";
			QMetaObject::invokeMethod(
				QCoreApplication::instance(),
				[] {
					QMessageBox::information(
						nullptr, "Hands Check",
						"Hands Check is linked. Completed reports will upload privately to your profile.");
				},
				Qt::QueuedConnection);
		}
		reply->deleteLater();
	});
}

void online_reports::unlink()
{
	clear_credential();
	implementation_->token.clear();
	implementation_->device_code.clear();
	implementation_->device_code_expires_at = {};
	implementation_->device_poll_timer.stop();
	implementation_->auth_required = false;
	implementation_->state = "Unlinked. Queued reports are retained locally.";
}

void online_reports::retry()
{
	implementation_->auth_required = false;
	for (auto &entry : implementation_->queue)
		entry.retry_at = QDateTime::currentDateTimeUtc();
	save_queue();
}

QString online_reports::status() const
{
	return implementation_->state;
}
bool online_reports::linked() const
{
	return !implementation_->token.isEmpty();
}

} // namespace sources::lol_game_report
