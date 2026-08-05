#include "lol_riot_api.hpp"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

#include <cstdlib>

#include <obs-module.h>

extern "C" {
#include <util/bmem.h>
}

namespace sources::lol_game_report {
namespace {
QString env_value(const QString &name)
{
	QStringList paths;
	if (char *module_path = obs_module_file("../../../../../.env")) {
		paths.append(QString::fromUtf8(module_path));
		bfree(module_path);
	}
	paths.append({QDir::current().filePath(".env"), QDir::home().filePath(".env"),
		      QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/.env"});
	for (const auto &path : paths) {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
			continue;
		while (!file.atEnd()) {
			QByteArray line = file.readLine().trimmed();
			line.remove(0, line.startsWith("export ") ? 7 : 0);
			if (!line.startsWith(name.toUtf8() + "="))
				continue;
			QString value = QString::fromUtf8(line.mid(name.size() + 1)).trimmed();
			if (value.size() >= 2 && ((value.startsWith('"') && value.endsWith('"')) ||
						  (value.startsWith('\'') && value.endsWith('\''))))
				value = value.mid(1, value.size() - 2);
			return value;
		}
	}
	return qEnvironmentVariable(name.toUtf8().constData());
}
QString routing_for(const QString &game_id)
{
	const QString platform = game_id.section('_', 0, 0).toUpper();
	if (QStringList{"NA1", "BR1", "LA1", "LA2", "OC1"}.contains(platform))
		return "americas";
	if (QStringList{"EUW1", "EUN1", "TR1", "RU"}.contains(platform))
		return "europe";
	if (QStringList{"KR", "JP1"}.contains(platform))
		return "asia";
	return "sea";
}
bool same_player(const QJsonObject &participant, const QString &player)
{
	return QString("%1#%2")
		       .arg(participant["riotIdGameName"].toString(), participant["riotIdTagline"].toString())
		       .compare(player, Qt::CaseInsensitive) == 0;
}

void apply_timeline(report &value, const QJsonObject &root, int participant_id, const QJsonObject &final_player)
{
	const QJsonArray frames = root["info"].toObject()["frames"].toArray();
	QVector<stat_sample> samples;
	QVector<event> events;
	QVector<item_event> items;
	QVector<ability_level> abilities;
	QHash<int, int> ability_levels;
	for (int frame_index = 0; frame_index < frames.size(); ++frame_index) {
		const QJsonObject frame = frames[frame_index].toObject();
		const int seconds = int(frame["timestamp"].toDouble() / 1000.0);
		const QJsonObject stats =
			frame["participantFrames"].toObject()[QString::number(participant_id)].toObject();
		if (!stats.isEmpty()) {
			stat_sample sample;
			sample.seconds = seconds;
			sample.cs = stats["minionsKilled"].toInt() + stats["jungleMinionsKilled"].toInt();
			sample.level = stats["level"].toInt();
			sample.gold = stats["currentGold"].toInt();
			sample.estimated_gold = stats["totalGold"].toInt();
			samples.append(sample);
		}
		for (int event_index = 0; event_index < frame["events"].toArray().size(); ++event_index) {
			const QJsonObject entry = frame["events"].toArray()[event_index].toObject();
			const QString type = entry["type"].toString();
			const int event_seconds = int(entry["timestamp"].toDouble() / 1000.0);
			QString detail, category;
			if (type == "CHAMPION_KILL" && (entry["killerId"].toInt() == participant_id ||
							entry["victimId"].toInt() == participant_id)) {
				detail = "ChampionKill";
				category = "kill";
			} else if (type == "ELITE_MONSTER_KILL") {
				detail = entry["monsterType"].toString() + "Kill";
				category = "objective";
			} else if (type == "BUILDING_KILL") {
				detail = entry["buildingType"].toString() == "TOWER_BUILDING" ? "TurretKilled"
											      : "BuildingKilled";
				category = "tower";
			} else if (type == "LEVEL_UP" && entry["participantId"].toInt() == participant_id) {
				detail = "LevelUp";
				category = "level";
			} else if (type == "SKILL_LEVEL_UP" && entry["participantId"].toInt() == participant_id) {
				const int slot = entry["skillSlot"].toInt();
				if (slot >= 1 && slot <= 4) {
					const QString name = QString("QWER").mid(slot - 1, 1);
					abilities.append({name, ++ability_levels[slot], event_seconds});
				}
			} else if (type == "ITEM_PURCHASED" && entry["participantId"].toInt() == participant_id) {
				const int item_id = entry["itemId"].toInt();
				items.append({QString("Item %1").arg(item_id), item_id, event_seconds});
				detail = "ItemPurchased";
				category = "item";
			}
			if (!category.isEmpty())
				events.append({QString("timeline-%1-%2").arg(frame_index).arg(event_index), detail,
					       event_seconds, detail, category});
		}
	}
	if (samples.isEmpty())
		return;
	stat_sample final = samples.last();
	final.seconds = value.duration_seconds;
	final.kills = final_player["kills"].toInt();
	final.deaths = final_player["deaths"].toInt();
	final.assists = final_player["assists"].toInt();
	final.cs = final_player["totalMinionsKilled"].toInt() + final_player["neutralMinionsKilled"].toInt();
	final.gold = final_player["goldEarned"].toInt();
	final.estimated_gold = final.gold;
	if (samples.last().seconds != final.seconds)
		samples.append(final);
	else
		samples.last() = final;
	value.samples = std::move(samples);
	value.events = std::move(events);
	value.item_events = std::move(items);
	if (!abilities.isEmpty())
		value.abilities = std::move(abilities);
	value.chapters = make_chapters(value.samples, value.events);
	value.enrichment["riot_match_v5_timeline"] = true;
}
} // namespace

riot_api::riot_api(QObject *parent) : QObject(parent), manager_(new QNetworkAccessManager(this)) {}

void riot_api::enrich(report value, std::function<void(report, QString)> complete)
{
	auto diagnose = [this](QJsonObject fields) {
		if (diagnostics_)
			diagnostics_(fields);
	};
	const QString key = env_value("RIOT_API_KEY");
	if (key.isEmpty()) {
		diagnose({{"outcome", "api_key_unavailable"}});
		complete(std::move(value), "Riot API key is unavailable.");
		return;
	}
	if (value.game_id.isEmpty()) {
		diagnose({{"outcome", "game_id_unavailable"}});
		complete(std::move(value), "The selected report has no Riot game ID.");
		return;
	}
	QNetworkRequest request(QUrl(QString("https://%1.api.riotgames.com/lol/match/v5/matches/%2")
					     .arg(routing_for(value.game_id), value.game_id)));
	request.setRawHeader("X-Riot-Token", key.toUtf8());
	request.setTransferTimeout(8000);
	const int local_duration_seconds = value.duration_seconds;
	const bool automatic_lookup = value.enrichment["automatic_match_lookup"].toBool();
	auto *reply = manager_->get(request);
	connect(reply, &QNetworkReply::finished, reply,
		[this, reply, key, local_duration_seconds, automatic_lookup, value = std::move(value),
		 complete = std::move(complete)]() mutable {
			auto diagnose = [this](QJsonObject fields) {
				if (diagnostics_)
					diagnostics_(fields);
			};
			QString status;
			if (reply->error() == QNetworkReply::NoError) {
				const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
				const QJsonObject info = root["info"].toObject();
				const QJsonArray players = info["participants"].toArray();
				const int match_duration_seconds = int(info["gameDuration"].toDouble());
				int self_team{}, participant_id{};
				QJsonObject self_player;
				for (const auto entry : players) {
					const QJsonObject player = entry.toObject();
					if (!same_player(player, value.player))
						continue;
					self_team = player["teamId"].toInt();
					participant_id = player["participantId"].toInt();
					self_player = player;
					value.outcome = player["win"].toBool() ? "Victory" : "Defeat";
					value.role = player["teamPosition"].toString();
					value.champion = player["championName"].toString();
					if (!value.samples.isEmpty()) {
						auto &last = value.samples.last();
						last.kills = player["kills"].toInt();
						last.deaths = player["deaths"].toInt();
						last.assists = player["assists"].toInt();
						last.cs = player["totalMinionsKilled"].toInt() +
							  player["neutralMinionsKilled"].toInt();
						last.gold = player["goldEarned"].toInt();
						last.estimated_gold = last.gold;
					}
				}
				if (self_team == 0)
					status = "Riot match found, but the report player was not a participant.";
				else if (automatic_lookup && local_duration_seconds > 0 &&
					 std::abs(match_duration_seconds - local_duration_seconds) > 120) {
					diagnose({{"request", "match"},
						  {"outcome", "duration_mismatch"},
						  {"local_duration_seconds", local_duration_seconds},
						  {"match_duration_seconds", match_duration_seconds}});
					reply->deleteLater();
					complete(
						std::move(value),
						"Automatic Riot lookup found a different recent match; enrichment remains pending.");
					return;
				}
				diagnose({{"request", "match"},
					  {"outcome", "success"},
					  {"http_status",
					   reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()},
					  {"participant_count", players.size()},
					  {"matched_local_player", self_team != 0}});
				value.team_gold = value.enemy_team_gold = value.team_kills = value.enemy_team_kills = 0;
				for (const auto entry : players) {
					const QJsonObject player = entry.toObject();
					const int gold = player["goldEarned"].toInt(), kills = player["kills"].toInt();
					if (player["teamId"].toInt() == self_team) {
						value.team_gold += gold;
						value.team_kills += kills;
					} else {
						value.enemy_team_gold += gold;
						value.enemy_team_kills += kills;
					}
				}
				if (self_team != 0) {
					value.duration_seconds =
						int(info["gameDuration"].toDouble(value.duration_seconds));
					const QString version = info["gameVersion"].toString();
					if (!version.isEmpty())
						value.assets["ddragon_version"] = version.section('.', 0, 2);
					value.enrichment["riot_match_v5"] = true;
					QNetworkRequest timeline_request(QUrl(
						QString("https://%1.api.riotgames.com/lol/match/v5/matches/%2/timeline")
							.arg(routing_for(value.game_id), value.game_id)));
					timeline_request.setRawHeader("X-Riot-Token", key.toUtf8());
					timeline_request.setTransferTimeout(8000);
					auto *timeline = manager_->get(timeline_request);
					connect(timeline, &QNetworkReply::finished, timeline,
						[this, timeline, value = std::move(value), participant_id, self_player,
						 complete = std::move(complete)]() mutable {
							QString timeline_status =
								"Riot Match-v5 enrichment complete; timeline unavailable.";
							if (timeline->error() == QNetworkReply::NoError) {
								const QJsonObject timeline_root =
									QJsonDocument::fromJson(timeline->readAll())
										.object();
								apply_timeline(value, timeline_root, participant_id,
									       self_player);
								timeline_status = "Riot Match-v5 enrichment complete.";
							}
							if (diagnostics_)
								diagnostics_(
									{{"request", "timeline"},
									 {"outcome",
									  timeline->error() == QNetworkReply::NoError
										  ? "success"
										  : "failure"},
									 {"http_status",
									  timeline->attribute(
											  QNetworkRequest::
												  HttpStatusCodeAttribute)
										  .toInt()},
									 {"frame_count", value.samples.size()},
									 {"event_count", value.events.size()},
									 {"item_count", value.item_events.size()}});
							timeline->deleteLater();
							complete(std::move(value), timeline_status);
						});
					reply->deleteLater();
					return;
				}
			} else {
				diagnose({{"request", "match"},
					  {"outcome", "failure"},
					  {"http_status",
					   reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()}});
				status =
					QString("Riot API request failed (HTTP %1).")
						.arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
			}
			reply->deleteLater();
			complete(std::move(value), status);
		});
}

void riot_api::enrich_latest(report value, std::function<void(report, QString)> complete)
{
	auto diagnose = [this](QJsonObject fields) {
		if (diagnostics_)
			diagnostics_(fields);
	};
	const QString key = env_value("RIOT_API_KEY");
	const QString platform = env_value("RIOT_PLATFORM").toUpper();
	const QString game_name = value.player.section('#', 0, 0);
	const QString tag_line = value.player.section('#', 1, 1);
	if (key.isEmpty() || platform.isEmpty() || game_name.isEmpty() || tag_line.isEmpty()) {
		diagnose({{"request", "automatic_match_lookup"}, {"outcome", "configuration_or_riot_id_unavailable"}});
		complete(std::move(value), "Automatic Riot enrichment is unavailable.");
		return;
	}
	const QString region = routing_for(platform);
	QNetworkRequest account_request(
		QUrl(QString("https://%1.api.riotgames.com/riot/account/v1/accounts/by-riot-id/%2/%3")
			     .arg(region, QString::fromUtf8(QUrl::toPercentEncoding(game_name)),
				  QString::fromUtf8(QUrl::toPercentEncoding(tag_line)))));
	account_request.setRawHeader("X-Riot-Token", key.toUtf8());
	account_request.setTransferTimeout(8000);
	auto *account = manager_->get(account_request);
	connect(account, &QNetworkReply::finished, account,
		[this, account, key, region, value = std::move(value), complete = std::move(complete)]() mutable {
			const int account_status = account->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			if (account->error() != QNetworkReply::NoError) {
				if (diagnostics_)
					diagnostics_({{"request", "account"},
						      {"outcome", "failure"},
						      {"http_status", account_status}});
				account->deleteLater();
				complete(std::move(value), "Automatic Riot account lookup failed.");
				return;
			}
			const QString puuid = QJsonDocument::fromJson(account->readAll()).object()["puuid"].toString();
			account->deleteLater();
			if (diagnostics_)
				diagnostics_({{"request", "account"},
					      {"outcome", "success"},
					      {"http_status", account_status},
					      {"matched_local_player", !puuid.isEmpty()}});
			if (puuid.isEmpty()) {
				complete(std::move(value), "Automatic Riot account lookup returned no PUUID.");
				return;
			}
			QNetworkRequest matches_request(QUrl(
				QString("https://%1.api.riotgames.com/lol/match/v5/matches/by-puuid/%2/ids?start=0&count=1")
					.arg(region, QString::fromUtf8(QUrl::toPercentEncoding(puuid)))));
			matches_request.setRawHeader("X-Riot-Token", key.toUtf8());
			matches_request.setTransferTimeout(8000);
			auto *matches = manager_->get(matches_request);
			connect(matches, &QNetworkReply::finished, matches,
				[this, matches, value = std::move(value), complete = std::move(complete)]() mutable {
					const int status =
						matches->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
					const QJsonArray ids =
						matches->error() == QNetworkReply::NoError
							? QJsonDocument::fromJson(matches->readAll()).array()
							: QJsonArray{};
					if (diagnostics_)
						diagnostics_({{"request", "match_ids"},
							      {"outcome", matches->error() == QNetworkReply::NoError
										  ? "success"
										  : "failure"},
							      {"http_status", status},
							      {"match_count", ids.size()}});
					matches->deleteLater();
					if (ids.isEmpty()) {
						complete(std::move(value),
							 "Automatic Riot match lookup found no recent match.");
						return;
					}
					value.game_id = ids.first().toString();
					value.enrichment["automatic_match_lookup"] = true;
					enrich(std::move(value), std::move(complete));
				});
		});
}
} // namespace sources::lol_game_report
