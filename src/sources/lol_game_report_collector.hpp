#pragma once

#include <QString>
#include <QJsonObject>
#include <QRect>

#include <cstdint>

namespace sources::lol_game_report {
enum class collection_state { empty, recording, finalizing };

class collector {
public:
	collector();
	~collector();
	collector(const collector &) = delete;
	collection_state state() const;
	void tick(int dpi, int hex_radius_percent);
	void set_dpi(int dpi);
	void set_hex_radius_percent(int radius_percent);
	void set_game_frame(const QRect &frame);
	void set_auto_open(bool enabled);
	void set_development_logs(bool enabled);
	bool development_logs_enabled() const;
	QString development_log_path() const;
	void log_riot_diagnostic(const QJsonObject &fields);
	QString recap_url() const;
	static QString state_text(collection_state value);

private:
	uint64_t cursor_{};
	bool discard_backlog_{true};
	bool development_logs_{};
};
} // namespace sources::lol_game_report
