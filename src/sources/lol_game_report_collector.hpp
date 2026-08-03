#pragma once

#include <QString>

namespace sources::lol_game_report {
enum class collection_state { empty, recording, finalizing };

class collector {
public:
	collector();
	~collector();
	collector(const collector &) = delete;
	collection_state state() const;
	static QString state_text(collection_state value);
};
} // namespace sources::lol_game_report
