#pragma once

#include "lol_game_report_types.hpp"

#include <QObject>
#include <QNetworkAccessManager>
#include <QTcpServer>

namespace sources::lol_game_report {

class web_server final : public QObject {
public:
	explicit web_server(QObject *parent = nullptr);
	QString url(const QString &report_id);
	void open(const report &value);

private:
	void respond(QTcpSocket *socket);
	void respond_ddragon(QTcpSocket *socket, const QString &path);
	QTcpServer server_;
	QNetworkAccessManager network_;
};

} // namespace sources::lol_game_report
