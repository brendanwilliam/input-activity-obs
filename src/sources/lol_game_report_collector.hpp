#pragma once

#include <QString>

#include <cstdint>

namespace sources::lol_game_report {
enum class collection_state { empty, recording, finalizing };

class collector {
public:
	collector();
	~collector();
	collector(const collector &) = delete;
	collection_state state() const;
	void tick(int dpi);
	void set_dpi(int dpi);
	void set_auto_open(bool enabled);
	QString recap_url() const;
	static QString state_text(collection_state value);

private:
	uint64_t cursor_{};
	bool discard_backlog_{true};
};
} // namespace sources::lol_game_report
