#include "lol_game_report_web.hpp"

#include "lol_game_report_store.hpp"

#include <QDesktopServices>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace sources::lol_game_report {
namespace {
constexpr auto page =
	R"(<!doctype html><meta charset="utf-8"><title>League Game Report</title><style>body{background:#10151f;color:#e9eef8;font:18px system-ui;margin:48px}h1{color:#8fc8ff}pre{white-space:pre-wrap;background:#182131;padding:24px;border-radius:8px}</style><h1>League Game Report</h1><div id="status">Loading local report…</div><pre id="report"></pre><script>const id=new URLSearchParams(location.search).get('report');fetch(id?'/api/report/'+encodeURIComponent(id):'/api/latest').then(r=>r.json()).then(x=>{status.textContent=x.player+' — '+x.game_mode;report.textContent=JSON.stringify(x,null,2)}).catch(()=>status.textContent='This local report is unavailable.');</script>)";
QByteArray response(const QByteArray &body, const char *type, int status = 200)
{
	return QByteArray("HTTP/1.1 ") + QByteArray::number(status) + (status == 200 ? " OK\r\n" : " Not Found\r\n") +
	       "Content-Type: " + type + "; charset=utf-8\r\nContent-Length: " + QByteArray::number(body.size()) +
	       "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" + body;
}
} // namespace

web_server::web_server(QObject *parent) : QObject(parent)
{
	connect(&server_, &QTcpServer::newConnection, this, [this] {
		while (auto *socket = server_.nextPendingConnection())
			connect(socket, &QTcpSocket::readyRead, socket, [this, socket] { respond(socket); });
	});
}

QString web_server::url(const QString &report_id)
{
	if (!server_.isListening() && !server_.listen(QHostAddress::LocalHost, 0))
		return {};
	QUrl result(QString("http://127.0.0.1:%1/").arg(server_.serverPort()));
	QUrlQuery query;
	query.addQueryItem("report", report_id);
	result.setQuery(query);
	return result.toString();
}

void web_server::open(const report &value)
{
	const QString address = url(value.id);
	if (!address.isEmpty())
		QMetaObject::invokeMethod(
			QCoreApplication::instance(), [address] { QDesktopServices::openUrl(QUrl(address)); },
			Qt::QueuedConnection);
}

void web_server::respond(QTcpSocket *socket)
{
	const QByteArray request = socket->readAll();
	const QList<QByteArray> words = request.left(request.indexOf('\n')).split(' ');
	if (words.size() < 2 || words.first() != "GET") {
		socket->write(response("Not found", "text/plain", 404));
		socket->disconnectFromHost();
		return;
	}
	const QString path = QUrl::fromEncoded(words[1]).path();
	if (path == "/") {
		socket->write(response(page, "text/html"));
	} else if (path == "/api/latest" || path.startsWith("/api/report/")) {
		const QString id = path == "/api/latest" ? QString() : path.mid(QString("/api/report/").size());
		const auto reports = store().reports();
		const auto it = id.isEmpty() ? reports.cbegin()
					     : std::find_if(reports.cbegin(), reports.cend(),
							    [&id](const auto &value) { return value.id == id; });
		if (it != reports.cend())
			socket->write(response(QJsonDocument(to_json(*it)).toJson(), "application/json"));
		else
			socket->write(response("Not found", "text/plain", 404));
	} else {
		socket->write(response("Not found", "text/plain", 404));
	}
	socket->disconnectFromHost();
}
} // namespace sources::lol_game_report
