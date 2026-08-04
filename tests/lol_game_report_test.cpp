#include "sources/lol_game_report_types.hpp"

#include <QJsonDocument>
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
	return 0;
}
