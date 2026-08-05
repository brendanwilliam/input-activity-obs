#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include "sources/game_report/collection/lol_hexbin.hpp"

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
	int estimated_gold{};
};

struct event {
	QString id;
	QString type;
	int seconds{};
	QString detail;
	QString category;
};

struct ability_level {
	QString ability;
	int level{};
	int seconds{};
};
struct item_event {
	QString item;
	int item_id{};
	int seconds{};
};
struct input_sample {
	int seconds{};
	int actions{};
	double mouse_distance_pixels{};
	double max_velocity_pixels_per_second{};
};
struct heatmap_bin {
	int x{};
	int y{};
	int count{};
};
struct chapter {
	int start_seconds{};
	int end_seconds{};
	QString summary;
};

struct report {
	int schema_version{3};
	QString id;
	QDateTime completed_at;
	QString player;
	QString game_mode;
	QString map;
	QString outcome{"unavailable"};
	QString champion;
	QString role;
	QString game_id;
	int duration_seconds{};
	int team_gold{};
	int enemy_team_gold{};
	int team_kills{};
	int enemy_team_kills{};
	QVector<stat_sample> samples;
	QVector<event> events;
	QStringList items;
	QStringList runes;
	QVector<ability_level> abilities;
	QVector<item_event> item_events;
	QVector<input_sample> input_samples;
	QVector<heatmap_bin> heatmap;
	hex_grid hex_geometry;
	QVector<hexbin> hexbins;
	bool hexbin_estimated{};
	int dpi{800};
	QJsonObject assets;
	QVector<chapter> chapters;
	QJsonObject enrichment;
};

struct insight {
	QString title;
	QString detail;
};

QJsonObject to_json(const report &value);
bool from_json(const QJsonObject &object, report &value);
QVector<chapter> make_chapters(const QVector<stat_sample> &samples, const QVector<event> &events);
QString classify_event(const QString &event_name);
QVector<insight> make_insights(const report &value);
QVector<double> normalized_series(const QVector<double> &values, bool average_ratio);
QString display_name(const report &value);

} // namespace sources::lol_game_report
