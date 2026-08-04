#include "sources/lol_game_report_types.hpp"
#include "sources/lol_game_report_ddragon.hpp"
#include "sources/lol_game_report_diagnostics.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QStandardPaths>
#include <cassert>

int main()
{
	using namespace sources::lol_game_report;
	report input;
	input.id = "report-1";
	input.player = "Self";
	input.game_mode = "CLASSIC";
	input.samples = {{0, 0, 0, 0, 0, 1, 500, 0}, {120, 2, 1, 3, 80, 6, 1200, 4}};
	input.events = {{"event-1", "ChampionKill", 115, "ChampionKill"}};
	input.chapters = make_chapters(input.samples, input.events);
	assert(!input.chapters.isEmpty());
	assert(!input.chapters.first().summary.contains("victory", Qt::CaseInsensitive));
	assert(!input.chapters.first().summary.contains("decisive", Qt::CaseInsensitive));
	report recovered;
	assert(from_json(QJsonDocument(to_json(input)).object(), recovered));
	assert(recovered.id == input.id && recovered.samples.size() == 2);
	assert(recovered.schema_version == 2);
	assert(classify_event("TurretKilled") == "tower");
	assert(classify_event("DragonKill") == "objective");
	const auto normalized = normalized_series({2.0, 4.0}, false);
	assert(normalized.size() == 2 && normalized.first() == 0.0 && normalized.last() == 1.0);
	const auto average_normalized = normalized_series({2.0, 4.0}, true);
	assert(average_normalized.size() == 2 && average_normalized.first() < 1.0 && average_normalized.last() > 1.0);
	assert(classify_event("RiftScuttlerKill") == "objective");
	QJsonObject legacy = to_json(input);
	legacy["schema_version"] = 1;
	legacy.remove("champion");
	assert(from_json(legacy, recovered));
	assert(recovered.schema_version == 1 && recovered.champion.isEmpty());
	QJsonObject raw_event{{"EventName", "ChampionKill"},
			      {"KillerName", "Self"},
			      {"VictimName", "Other"},
			      {"EventTime", 12.0}};
	QJsonObject events{{"Events", QJsonArray{raw_event}}};
	const QJsonObject sanitized = sanitize_eventdata(events);
	const QJsonObject sanitized_event = sanitized["Events"].toArray().first().toObject();
	assert(sanitized_event["EventName"] == "ChampionKill");
	assert(sanitized_event["KillerName"] == "redacted");
	assert(sanitized_event["VictimName"] == "redacted");
	const QJsonObject playerlist{
		{"allPlayers", QJsonArray{QJsonObject{{"team", "ORDER"}, {"summonerName", "Never logged"}},
					  QJsonObject{{"team", "CHAOS"}, {"riotId", "Never logged"}}}}};
	const QJsonObject summary = summarize_playerlist(playerlist);
	assert(summary["row_count"] == 2 && summary["team_counts"].toObject()["ORDER"] == 1);
	assert(!QJsonDocument(summary).toJson().contains("Never logged"));
	const QJsonObject ability_metadata = QJsonDocument::fromJson(R"({
        "data": {"Ahri": {"spells": [
          {"image": {"full": "AhriOrbofDeception.png"}},
          {"image": {"full": "AhriFoxFire.png"}},
          {"image": {"full": "AhriSeduce.png"}},
          {"image": {"full": "AhriTumble.png"}}
        ]}}
      })")
						     .object();
	assert(ability_icon_filename(ability_metadata, "Ahri", 'Q') == "AhriOrbofDeception.png");
	assert(ability_icon_filename(ability_metadata, "Missing", 'Q').isEmpty());
	QJsonObject missing_spells_metadata = ability_metadata;
	QJsonObject data = missing_spells_metadata.value("data").toObject();
	QJsonObject champion = data.value("Ahri").toObject();
	champion.remove("spells");
	data["Ahri"] = champion;
	missing_spells_metadata["data"] = data;
	assert(ability_icon_filename(missing_spells_metadata, "Ahri", 'Q').isEmpty());
	QJsonObject out_of_range_metadata = ability_metadata;
	data = out_of_range_metadata.value("data").toObject();
	champion = data.value("Ahri").toObject();
	champion["spells"] = QJsonArray{};
	data["Ahri"] = champion;
	out_of_range_metadata["data"] = data;
	assert(ability_icon_filename(out_of_range_metadata, "Ahri", 'Q').isEmpty());
	assert(ability_icon_filename(ability_metadata, "Ahri", 'X').isEmpty());
	QJsonObject missing_image_metadata = ability_metadata;
	data = missing_image_metadata.value("data").toObject();
	champion = data.value("Ahri").toObject();
	champion["spells"] = QJsonArray{QJsonObject{}};
	data["Ahri"] = champion;
	missing_image_metadata["data"] = data;
	assert(ability_icon_filename(missing_image_metadata, "Ahri", 'Q').isEmpty());
	QStandardPaths::setTestModeEnabled(true);
	diagnostic_log log;
	assert(log.path().isEmpty());
	log.set_enabled(true);
	const QString log_path = log.path();
	assert(!log_path.isEmpty() && QFile::exists(log_path));
	log.write("input", "input_accepted_zero_clock", {{"action_count", 3}});
	log.set_enabled(false);
	assert(QFile::exists(log_path));
	log.close_and_remove();
	assert(!QFile::exists(log_path));
	return 0;
}
