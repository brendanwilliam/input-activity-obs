#pragma once

#include <cstdint>
#include <memory>

namespace sources {

class lol_game_start_detector {
public:
	uint64_t observe(bool game_active)
	{
		if (!game_active)
			armed_ = true;
		if (armed_ && game_active && !game_active_)
			++starts_;
		game_active_ = game_active;
		return starts_;
	}

private:
	bool armed_{};
	bool game_active_{};
	uint64_t starts_{};
};

class lol_dashboard_game_start_watcher {
public:
	lol_dashboard_game_start_watcher();
	~lol_dashboard_game_start_watcher();
	lol_dashboard_game_start_watcher(const lol_dashboard_game_start_watcher &) = delete;

	bool consume_start(uint64_t &cursor) const;

private:
	struct implementation;
	std::unique_ptr<implementation> implementation_;
};

} // namespace sources
