#pragma once

#include "lol_game_report_riot_api.hpp"
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
	void respond_game(QTcpSocket *socket, const QString &game_id);
	void respond_ddragon(QTcpSocket *socket, const QString &path);
	QTcpServer server_;
	QNetworkAccessManager network_;
	riot_api riot_{this};
};

} // namespace sources::lol_game_report
