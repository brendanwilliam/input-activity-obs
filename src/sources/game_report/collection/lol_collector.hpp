#pragma once

#include "sources/game_report/data/lol_types.hpp"

#include <QString>
#include <QJsonObject>
#include <QRect>

#include <cstdint>
#include <functional>

namespace sources::lol_game_report {
enum class collection_state { empty, recording, finalizing };

class collector {
public:
	collector();
	~collector();
	collector(const collector &) = delete;
	collection_state state() const;
	void tick(int dpi, double hex_radius_percent);
	void set_dpi(int dpi);
	void set_hex_radius_percent(double radius_percent);
	void set_game_frame(const QRect &frame);
	void set_submission_callback(std::function<void(const report &)> callback);
	void set_development_logs(bool enabled);
	bool development_logs_enabled() const;
	QString development_log_path() const;
	static QString state_text(collection_state value);

private:
	uint64_t cursor_{};
	bool discard_backlog_{true};
	bool development_logs_{};
};
} // namespace sources::lol_game_report
