#pragma once

#include <QString>

#include <utility>

namespace sources {

class lol_dashboard_game_config_watcher {
public:
	void set_path(const QString &path);
	bool changed(float seconds);

private:
	QString path_;
	std::pair<qint64, qint64> stamp_{};
	float poll_seconds_{};
};

} // namespace sources
