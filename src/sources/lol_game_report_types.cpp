#include "lol_game_report_types.hpp"

#include <QJsonArray>

namespace sources::lol_game_report {
namespace {
QJsonObject sample_json(const stat_sample &sample)
{
	return {{"seconds", sample.seconds}, {"kills", sample.kills},
		{"deaths", sample.deaths},   {"assists", sample.assists},
		{"cs", sample.cs},           {"level", sample.level},
		{"gold", sample.gold},       {"ward_score", sample.ward_score}};
}

stat_sample sample_from_json(const QJsonObject &object)
{
	return {object["seconds"].toInt(), object["kills"].toInt(),     object["deaths"].toInt(),
		object["assists"].toInt(), object["cs"].toInt(),        object["level"].toInt(),
		object["gold"].toInt(),    object["ward_score"].toInt()};
}
} // namespace

QJsonObject to_json(const report &value)
{
	QJsonArray samples, events, chapters, items, runes;
	for (const auto &sample : value.samples)
		samples.append(sample_json(sample));
	for (const auto &event : value.events)
		events.append(QJsonObject{{"id", event.id},
					  {"type", event.type},
					  {"seconds", event.seconds},
					  {"detail", event.detail}});
	for (const auto &chapter : value.chapters)
		chapters.append(QJsonObject{{"start_seconds", chapter.start_seconds},
					    {"end_seconds", chapter.end_seconds},
					    {"summary", chapter.summary}});
	for (const auto &item : value.items)
		items.append(item);
	for (const auto &rune : value.runes)
		runes.append(rune);
	return {{"schema_version", value.schema_version},
		{"id", value.id},
		{"completed_at", value.completed_at.toUTC().toString(Qt::ISODateWithMs)},
		{"player", value.player},
		{"game_mode", value.game_mode},
		{"map", value.map},
		{"outcome", value.outcome},
		{"samples", samples},
		{"events", events},
		{"items", items},
		{"runes", runes},
		{"chapters", chapters},
		{"enrichment", value.enrichment}};
}

bool from_json(const QJsonObject &object, report &value)
{
	if (object["schema_version"].toInt() != 1 || object["id"].toString().isEmpty())
		return false;
	value.schema_version = 1;
	value.id = object["id"].toString();
	value.completed_at = QDateTime::fromString(object["completed_at"].toString(), Qt::ISODateWithMs);
	value.player = object["player"].toString();
	value.game_mode = object["game_mode"].toString();
	value.map = object["map"].toString();
	value.outcome = object["outcome"].toString("unavailable");
	value.samples.clear();
	value.events.clear();
	value.items.clear();
	value.runes.clear();
	value.chapters.clear();
	const auto samples = object["samples"].toArray();
	const auto events = object["events"].toArray();
	const auto items = object["items"].toArray();
	const auto runes = object["runes"].toArray();
	const auto chapters = object["chapters"].toArray();
	for (const QJsonValue item : samples)
		value.samples.append(sample_from_json(item.toObject()));
	for (const QJsonValue item : events) {
		const auto o = item.toObject();
		value.events.append(
			{o["id"].toString(), o["type"].toString(), o["seconds"].toInt(), o["detail"].toString()});
	}
	for (const QJsonValue item : items)
		value.items.append(item.toString());
	for (const QJsonValue item : runes)
		value.runes.append(item.toString());
	for (const QJsonValue item : chapters) {
		const auto o = item.toObject();
		value.chapters.append({o["start_seconds"].toInt(), o["end_seconds"].toInt(), o["summary"].toString()});
	}
	value.enrichment = object["enrichment"].toObject();
	return true;
}

QVector<chapter> make_chapters(const QVector<stat_sample> &samples, const QVector<event> &events)
{
	QVector<chapter> result;
	if (samples.isEmpty())
		return result;
	QVector<int> boundaries{samples.first().seconds};
	for (const auto &event : events)
		if (boundaries.isEmpty() || event.seconds - boundaries.last() > 90)
			boundaries.append(event.seconds);
	boundaries.append(samples.last().seconds + 1);
	for (int index = 0; index + 1 < boundaries.size(); ++index) {
		const int start = boundaries[index], end = boundaries[index + 1] - 1;
		const auto first = std::find_if(samples.cbegin(), samples.cend(),
						[start](const auto &s) { return s.seconds >= start; });
		const auto last = std::find_if(samples.crbegin(), samples.crend(),
					       [end](const auto &s) { return s.seconds <= end; });
		if (first == samples.cend() || last == samples.crend())
			continue;
		QStringList changes;
		if (last->kills != first->kills || last->deaths != first->deaths || last->assists != first->assists)
			changes << QString("K/D/A %1/%2/%3 → %4/%5/%6")
					   .arg(first->kills)
					   .arg(first->deaths)
					   .arg(first->assists)
					   .arg(last->kills)
					   .arg(last->deaths)
					   .arg(last->assists);
		if (last->cs != first->cs)
			changes << QString("CS %1 → %2").arg(first->cs).arg(last->cs);
		if (last->level != first->level)
			changes << QString("level %1 → %2").arg(first->level).arg(last->level);
		if (last->gold != first->gold)
			changes << QString("current gold %1 → %2").arg(first->gold).arg(last->gold);
		if (changes.isEmpty())
			changes << "observed self-stat sample";
		result.append({start, end, QString("Activity window: %1.").arg(changes.join(", "))});
	}
	return result;
}

QString display_name(const report &value)
{
	return QString("%1 — %2").arg(value.completed_at.toLocalTime().toString("yyyy-MM-dd HH:mm"),
				      value.game_mode.isEmpty() ? "League game" : value.game_mode);
}
} // namespace sources::lol_game_report
