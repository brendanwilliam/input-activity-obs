#include "lol_dashboard_game_config_watcher.hpp"

#include <QFileInfo>

namespace sources {

void lol_dashboard_game_config_watcher::set_path(const QString &path)
{
	path_ = path;
	stamp_ = {};
	poll_seconds_ = 0.0F;
}

bool lol_dashboard_game_config_watcher::changed(float seconds)
{
	poll_seconds_ += seconds;
	if (poll_seconds_ < 0.5F)
		return false;
	poll_seconds_ = 0.0F;
	const QFileInfo info(path_);
	const std::pair<qint64, qint64> next{info.lastModified().toMSecsSinceEpoch(), info.size()};
	if (next == stamp_)
		return false;
	stamp_ = next;
	return true;
}

} // namespace sources
