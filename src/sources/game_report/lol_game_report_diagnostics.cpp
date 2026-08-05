#include "lol_game_report_diagnostics.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

namespace sources::lol_game_report {
namespace {
bool identity_key(const QString &key)
{
	const QString lower = key.toLower();
	return lower.contains("killer") || lower.contains("victim") || lower.contains("player") ||
	       lower.contains("summoner") || lower.contains("riotid") || lower.contains("puuid");
}
} // namespace

QJsonObject sanitize_eventdata(const QJsonObject &payload)
{
	QJsonObject result;
	QJsonArray events;
	for (const QJsonValue value : payload.value("Events").toArray()) {
		const QJsonObject source = value.toObject();
		QJsonObject event;
		for (auto it = source.begin(); it != source.end(); ++it)
			event.insert(it.key(), identity_key(it.key()) ? QJsonValue("redacted") : it.value());
		events.append(event);
	}
	result.insert("Events", events);
	return result;
}

QJsonObject summarize_playerlist(const QJsonObject &payload)
{
	QJsonObject result;
	const QJsonArray players = payload.value("allPlayers").toArray();
	QJsonObject teams;
	for (const QJsonValue value : players) {
		const QString team = value.toObject().value("team").toString("unknown");
		teams.insert(team, teams.value(team).toInt() + 1);
	}
	result.insert("available", !payload.isEmpty());
	result.insert("row_count", players.size());
	result.insert("team_counts", teams);
	return result;
}

void diagnostic_log::set_enabled(bool enabled)
{
	enabled_ = enabled;
	if (enabled_)
		open();
}

void diagnostic_log::open()
{
	if (file_ || !enabled_)
		return;
	const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
			     "/league-game-reports/development-logs";
	if (!QDir().mkpath(root))
		return;
	const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMddTHHmmsszzzZ");
	path_ = root + "/session-" + stamp + ".jsonl";
	auto *candidate = new QFile(path_);
	if (!candidate->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
		delete candidate;
		path_.clear();
		return;
	}
	candidate->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	file_ = candidate;
	write("diagnostics", "session_started");
}

void diagnostic_log::write(const QString &component, const QString &event, QJsonObject fields)
{
	if (!enabled_)
		return;
	open();
	if (!file_)
		return;
	fields.insert("timestamp_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
	fields.insert("sequence", QString::number(++sequence_));
	fields.insert("component", component);
	fields.insert("event", event);
	if (file_->write(QJsonDocument(fields).toJson(QJsonDocument::Compact) + '\n') < 0) {
		file_->close();
		delete file_;
		file_ = nullptr;
	}
}

void diagnostic_log::close_and_remove()
{
	if (file_) {
		file_->close();
		delete file_;
		file_ = nullptr;
	}
	if (!path_.isEmpty())
		QFile::remove(path_);
	path_.clear();
	sequence_ = 0;
}
} // namespace sources::lol_game_report
