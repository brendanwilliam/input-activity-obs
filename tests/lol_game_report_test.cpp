#include "sources/game_report/collection/lol_hexbin.hpp"
#include "sources/game_report/collection/lol_input_telemetry.hpp"
#include "sources/game_report/data/lol_diagnostics.hpp"
#include "sources/game_report/data/lol_types.hpp"
#include "sources/game_report/integration/lol_ddragon.hpp"
#include "sources/game_report/presentation/lol_web_assets.hpp"

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
	input.hex_geometry = {1.6, 7};
	input.hexbins = {{2, 3, 250}, {4, 5, 1000}};
	assert(from_json(QJsonDocument(to_json(input)).object(), recovered));
	assert(recovered.id == input.id && recovered.samples.size() == 2);
	assert(recovered.schema_version == 4 && recovered.hex_geometry.radius_percent == 7);
	assert(recovered.hex_geometry.frame_aspect_ratio == 1.6 && recovered.hexbins.size() == 2 &&
	       recovered.hexbins.last().dwell_ms == 1000);
	assert(classify_event("TurretKilled") == "tower");
	assert(classify_event("DragonKill") == "objective");
	const auto normalized = normalized_series({2.0, 4.0}, false);
	assert(normalized.size() == 2 && normalized.first() == 0.0 && normalized.last() == 1.0);
	const auto average_normalized = normalized_series({2.0, 4.0}, true);
	assert(average_normalized.size() == 2 && average_normalized.first() < 1.0 && average_normalized.last() > 1.0);
	assert(classify_event("RiftScuttlerKill") == "objective");
	QJsonObject legacy = to_json(input);
	legacy["schema_version"] = 1;
	legacy.remove("hexbins");
	legacy["heatmap"] = QJsonArray{QJsonObject{{"x", 5}, {"y", 4}, {"count", 24}}};
	legacy.remove("champion");
	assert(from_json(legacy, recovered));
	assert(recovered.schema_version == 4 && recovered.champion.isEmpty());
	assert(recovered.hexbin_estimated && recovered.hexbins.size() == 1 && recovered.hexbins.first().dwell_ms == 24);
	const hex_grid grid{1.6, 4};
	assert(canonical_height(grid) == 62.5);
	assert(nearest_hex(grid, hex_center(grid, 3, 2)).column == 3);
	assert(nearest_hex(grid, hex_center(grid, 3, 2)).row == 2);
	const auto cells = sources::lol_heatmap::visible_cells(grid);
	assert(!cells.isEmpty() && cells.first().column == 0 && cells.first().row == 0);
	const QByteArray &report_script = web_assets::script();
	if (!report_script.contains("frame_aspect_ratio || 16 / 9))} / 1") ||
	    !report_script.contains("const columns = Math.ceil(width / hexWidth) + 1") ||
	    !report_script.contains("const x = hexWidth * (column + offset)") ||
	    !report_script.contains(", y = radius * (1 + 1.5 * row)"))
		return 1;
	(void)grid;
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
	input_telemetry telemetry;
	telemetry.set_game_frame({1000, 100, 1000, 500});
	QVector<input_sample> input_samples;
	QVector<hexbin> hexbins;
	std::vector<input_data::trace_event> input_events{
		{1, 1000000000ULL, EVENT_KEY_PRESSED, 12},
		{2, 1100000000ULL, EVENT_KEY_RELEASED, 12},
		{3, 1200000000ULL, EVENT_MOUSE_PRESSED, 1},
		{4, 1300000000ULL, EVENT_MOUSE_MOVED, 0, 1100, 200},
		{5, 2300000000ULL, EVENT_MOUSE_MOVED, 0, 1500, 200},
		{6, 2400000000ULL, EVENT_MOUSE_MOVED, 0, 500, 200},
		{7, 2500000000ULL, EVENT_MOUSE_MOVED, 0, 1600, 200},
	};
	telemetry.consume(input_events, 60, input_samples, hexbins);
	assert(input_samples.size() == 2);
	assert(input_samples[0].actions == 2 && input_samples[1].actions == 0);
	assert(input_samples[1].mouse_distance_pixels == 400.0);
	assert(input_samples[1].max_velocity_pixels_per_second == 400.0);
	assert(hexbins.size() == 1 && hexbins.first().dwell_ms == dwell_gap_limit_ms);
	if (hexbins.first().column != nearest_hex({2.0, 4.0}, {10.0, 10.0}).column ||
	    hexbins.first().row != nearest_hex({2.0, 4.0}, {10.0, 10.0}).row)
		return 1;
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
