#pragma once

#include "lol_game_report_types.hpp"

#include <QObject>
#include <QTcpServer>

namespace sources::lol_game_report {

class web_server final : public QObject {
public:
	explicit web_server(QObject *parent = nullptr);
	QString url(const QString &report_id);
	void open(const report &value);

private:
	void respond(QTcpSocket *socket);
	QTcpServer server_;
};

} // namespace sources::lol_game_report
