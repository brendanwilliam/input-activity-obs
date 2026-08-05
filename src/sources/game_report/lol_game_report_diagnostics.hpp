#pragma once

#include <QJsonObject>
#include <QString>

class QFile;

namespace sources::lol_game_report {

QJsonObject sanitize_eventdata(const QJsonObject &payload);
QJsonObject summarize_playerlist(const QJsonObject &payload);

class diagnostic_log {
public:
	void set_enabled(bool enabled);
	bool enabled() const { return enabled_; }
	QString path() const { return path_; }
	void write(const QString &component, const QString &event, QJsonObject fields = {});
	void close_and_remove();

private:
	void open();
	bool enabled_{};
	quint64 sequence_{};
	QString path_;
	QFile *file_{};
};

} // namespace sources::lol_game_report
