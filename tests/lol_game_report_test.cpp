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
	assert(!input.chapters.first().summary.contains("win", Qt::CaseInsensitive));
	assert(!input.chapters.first().summary.contains("decisive", Qt::CaseInsensitive));
	report recovered;
	assert(from_json(QJsonDocument(to_json(input)).object(), recovered));
	assert(recovered.id == input.id && recovered.samples.size() == 2);
	return 0;
}
