#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace sources::lol_game_report {

struct stat_sample {
	int seconds{};
	int kills{};
	int deaths{};
	int assists{};
	int cs{};
	int level{};
	int gold{};
	int ward_score{};
};

struct event {
	QString id;
	QString type;
	int seconds{};
	QString detail;
};

struct chapter {
	int start_seconds{};
	int end_seconds{};
	QString summary;
};

struct report {
	int schema_version{1};
	QString id;
	QDateTime completed_at;
	QString player;
	QString game_mode;
	QString map;
	QString outcome{"unavailable"};
	QVector<stat_sample> samples;
	QVector<event> events;
	QStringList items;
	QStringList runes;
	QVector<chapter> chapters;
	QJsonObject enrichment;
};

QJsonObject to_json(const report &value);
bool from_json(const QJsonObject &object, report &value);
QVector<chapter> make_chapters(const QVector<stat_sample> &samples, const QVector<event> &events);
QString display_name(const report &value);

} // namespace sources::lol_game_report
