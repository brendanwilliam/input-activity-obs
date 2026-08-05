#include "sources/game_report/data/lol_types.hpp"

#include <QJsonArray>
#include <algorithm>
#include <numeric>

namespace sources::lol_game_report {
namespace {
QJsonObject sample_json(const stat_sample &s)
{
	return {{"seconds", s.seconds},
		{"kills", s.kills},
		{"deaths", s.deaths},
		{"assists", s.assists},
		{"cs", s.cs},
		{"level", s.level},
		{"gold", s.gold},
		{"ward_score", s.ward_score},
		{"estimated_gold", s.estimated_gold}};
}
stat_sample sample_from_json(const QJsonObject &o)
{
	return {o["seconds"].toInt(), o["kills"].toInt(),      o["deaths"].toInt(),
		o["assists"].toInt(), o["cs"].toInt(),         o["level"].toInt(),
		o["gold"].toInt(),    o["ward_score"].toInt(), o["estimated_gold"].toInt(o["gold"].toInt())};
}
QJsonArray strings_json(const QStringList &values)
{
	QJsonArray result;
	for (const auto &value : values)
		result.append(value);
	return result;
}
} // namespace

QJsonObject to_json(const report &v)
{
	QJsonArray samples, events, abilities, item_events, input, hexbins, chapters;
	for (const auto &s : v.samples)
		samples.append(sample_json(s));
	for (const auto &e : v.events)
		events.append(QJsonObject{{"id", e.id},
					  {"type", e.type},
					  {"seconds", e.seconds},
					  {"detail", e.detail},
					  {"category", e.category}});
	for (const auto &a : v.abilities)
		abilities.append(QJsonObject{{"ability", a.ability}, {"level", a.level}, {"seconds", a.seconds}});
	for (const auto &i : v.item_events)
		item_events.append(QJsonObject{{"item", i.item}, {"item_id", i.item_id}, {"seconds", i.seconds}});
	for (const auto &i : v.input_samples)
		input.append(QJsonObject{{"seconds", i.seconds},
					 {"actions", i.actions},
					 {"mouse_distance_pixels", i.mouse_distance_pixels},
					 {"max_velocity_pixels_per_second", i.max_velocity_pixels_per_second}});
	for (const auto &h : v.hexbins)
		hexbins.append(QJsonObject{{"column", h.column}, {"row", h.row}, {"dwell_ms", double(h.dwell_ms)}});
	for (const auto &c : v.chapters)
		chapters.append(QJsonObject{{"start_seconds", c.start_seconds},
					    {"end_seconds", c.end_seconds},
					    {"summary", c.summary}});
	return {{"schema_version", 3},
		{"id", v.id},
		{"completed_at", v.completed_at.toUTC().toString(Qt::ISODateWithMs)},
		{"player", v.player},
		{"game_mode", v.game_mode},
		{"map", v.map},
		{"outcome", v.outcome},
		{"champion", v.champion},
		{"role", v.role},
		{"game_id", v.game_id},
		{"duration_seconds", v.duration_seconds},
		{"team_gold", v.team_gold},
		{"enemy_team_gold", v.enemy_team_gold},
		{"team_kills", v.team_kills},
		{"enemy_team_kills", v.enemy_team_kills},
		{"samples", samples},
		{"events", events},
		{"items", strings_json(v.items)},
		{"runes", strings_json(v.runes)},
		{"abilities", abilities},
		{"item_events", item_events},
		{"input_samples", input},
		{"hex_radius_percent", v.hex_geometry.radius_percent},
		{"frame_aspect_ratio", v.hex_geometry.frame_aspect_ratio},
		{"hexbins", hexbins},
		{"hexbin_estimated", v.hexbin_estimated},
		{"dpi", v.dpi},
		{"assets", v.assets},
		{"chapters", chapters},
		{"enrichment", v.enrichment}};
}

bool from_json(const QJsonObject &o, report &v)
{
	const int version = o["schema_version"].toInt();
	if ((version < 1 || version > 3) || o["id"].toString().isEmpty())
		return false;
	v = {};
	v.schema_version = version;
	v.id = o["id"].toString();
	v.completed_at = QDateTime::fromString(o["completed_at"].toString(), Qt::ISODateWithMs);
	v.player = o["player"].toString();
	v.game_mode = o["game_mode"].toString();
	v.map = o["map"].toString();
	v.outcome = o["outcome"].toString("unavailable");
	v.champion = o["champion"].toString();
	v.role = o["role"].toString();
	v.game_id = o["game_id"].toString();
	v.duration_seconds = o["duration_seconds"].toInt();
	v.team_gold = o["team_gold"].toInt();
	v.enemy_team_gold = o["enemy_team_gold"].toInt();
	v.team_kills = o["team_kills"].toInt();
	v.enemy_team_kills = o["enemy_team_kills"].toInt();
	v.dpi = o["dpi"].toInt(800);
	v.assets = o["assets"].toObject();
	v.enrichment = o["enrichment"].toObject();
	for (const auto x : o["samples"].toArray())
		v.samples.append(sample_from_json(x.toObject()));
	for (const auto x : o["events"].toArray()) {
		const auto e = x.toObject();
		v.events.append({e["id"].toString(), e["type"].toString(), e["seconds"].toInt(), e["detail"].toString(),
				 e["category"].toString(classify_event(e["type"].toString()))});
	}
	for (const auto x : o["items"].toArray())
		v.items.append(x.toString());
	for (const auto x : o["runes"].toArray())
		v.runes.append(x.toString());
	for (const auto x : o["abilities"].toArray()) {
		const auto a = x.toObject();
		v.abilities.append({a["ability"].toString(), a["level"].toInt(), a["seconds"].toInt()});
	}
	for (const auto x : o["item_events"].toArray()) {
		const auto i = x.toObject();
		v.item_events.append({i["item"].toString(), i["item_id"].toInt(), i["seconds"].toInt()});
	}
	for (const auto x : o["input_samples"].toArray()) {
		const auto i = x.toObject();
		v.input_samples.append({i["seconds"].toInt(), i["actions"].toInt(),
					i["mouse_distance_pixels"].toDouble(),
					i["max_velocity_pixels_per_second"].toDouble()});
	}
	if (version == 3) {
		v.hex_geometry.radius_percent =
			std::clamp(o["hex_radius_percent"].toInt(default_hex_radius_percent), 1, 20);
		v.hex_geometry.frame_aspect_ratio = std::max(0.01, o["frame_aspect_ratio"].toDouble(16.0 / 9.0));
		v.hexbin_estimated = o["hexbin_estimated"].toBool();
		for (const auto x : o["hexbins"].toArray()) {
			const auto h = x.toObject();
			v.hexbins.append({h["column"].toInt(), h["row"].toInt(), uint64_t(h["dwell_ms"].toDouble())});
		}
	} else {
		v.hexbin_estimated = true;
		for (const auto x : o["heatmap"].toArray()) {
			const auto h = x.toObject();
			const QPointF point((h["x"].toInt() + 0.5) * 100.0 / 30.0,
					    (h["y"].toInt() + 0.5) * canonical_height(v.hex_geometry) / 17.0);
			add_hex_dwell(v.hexbins, nearest_hex(v.hex_geometry, point),
				      uint64_t(std::max(0, h["count"].toInt())));
		}
		v.schema_version = 3;
	}
	for (const auto x : o["chapters"].toArray()) {
		const auto c = x.toObject();
		v.chapters.append({c["start_seconds"].toInt(), c["end_seconds"].toInt(), c["summary"].toString()});
	}
	return true;
}

QString classify_event(const QString &name)
{
	if (name == "ChampionKill")
		return "kill";
	if (name.contains("Turret", Qt::CaseInsensitive))
		return "tower";
	if (name.contains("Dragon", Qt::CaseInsensitive) || name.contains("Baron", Qt::CaseInsensitive) ||
	    name.contains("Herald", Qt::CaseInsensitive) || name.contains("RiftScuttler", Qt::CaseInsensitive))
		return "objective";
	if (name == "LevelUp")
		return "level";
	return name == "GameEnd" ? "game_end" : "other";
}

QVector<chapter> make_chapters(const QVector<stat_sample> &samples, const QVector<event> &events)
{
	QVector<chapter> result;
	if (samples.isEmpty())
		return result;
	QVector<int> boundaries{samples.first().seconds};
	for (const auto &e : events)
		if (e.seconds - boundaries.last() > 90)
			boundaries.append(e.seconds);
	boundaries.append(samples.last().seconds + 1);
	for (int n = 0; n + 1 < boundaries.size(); ++n) {
		const int start = boundaries[n], end = boundaries[n + 1] - 1;
		auto first = std::find_if(samples.cbegin(), samples.cend(),
					  [&](const auto &s) { return s.seconds >= start; });
		auto last = std::find_if(samples.crbegin(), samples.crend(),
					 [&](const auto &s) { return s.seconds <= end; });
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
		result.append({start, end, changes.isEmpty() ? "Observed activity window." : changes.join(", ")});
	}
	return result;
}

QVector<insight> make_insights(const report &v)
{
	QVector<insight> result;
	if (!v.item_events.isEmpty())
		result.append({"First completed item",
			       QString("%1 at %2:%3")
				       .arg(v.item_events.first().item)
				       .arg(v.item_events.first().seconds / 60)
				       .arg(v.item_events.first().seconds % 60, 2, 10, QLatin1Char('0'))});
	for (const auto &a : v.abilities)
		if (a.level == 6 || a.level == 11 || a.level == 16)
			result.append({"Level milestone", QString("Level %1 at %2:%3")
								  .arg(a.level)
								  .arg(a.seconds / 60)
								  .arg(a.seconds % 60, 2, 10, QLatin1Char('0'))});
	return result;
}

QVector<double> normalized_series(const QVector<double> &values, bool average_ratio)
{
	QVector<double> result;
	if (values.isEmpty())
		return result;
	const double average = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
	const auto [low, high] = std::minmax_element(values.begin(), values.end());
	const double denominator = average_ratio ? average : *high - *low;
	for (double value : values)
		result.append(denominator == 0 ? 0.0
					       : (average_ratio ? value / denominator : (value - *low) / denominator));
	return result;
}
QString display_name(const report &v)
{
	return QString("%1 — %2").arg(v.completed_at.toLocalTime().toString("yyyy-MM-dd HH:mm"),
				      v.game_mode.isEmpty() ? "League game" : v.game_mode);
}
} // namespace sources::lol_game_report
