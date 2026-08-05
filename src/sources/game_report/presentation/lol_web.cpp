#include "sources/game_report/presentation/lol_web.hpp"

#include "sources/game_report/data/lol_store.hpp"
#include "sources/game_report/integration/lol_ddragon.hpp"
#include "sources/game_report/presentation/lol_web_assets.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QUuid>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace sources::lol_game_report {
namespace {
QByteArray response(const QByteArray &body, const char *type, int status = 200)
{
	return QByteArray("HTTP/1.1 ") + QByteArray::number(status) + (status == 200 ? " OK\r\n" : " Not Found\r\n") +
	       "Content-Type: " + type + "; charset=utf-8\r\nContent-Length: " + QByteArray::number(body.size()) +
	       "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" + body;
}
bool safe_path_part(const QString &value)
{
	return !value.isEmpty() && value.size() < 96 && !value.contains("..") && !value.contains('/');
}
QByteArray error_response(const QString &message, int status = 404)
{
	return response(QJsonDocument(QJsonObject{{"error", message}}).toJson(QJsonDocument::Compact),
			"application/json", status);
}
bool valid_game_id(const QString &value)
{
	const int separator = value.indexOf('_');
	return safe_path_part(value) && separator > 0 && separator + 1 < value.size() &&
	       std::all_of(value.cbegin(), value.cend(), [separator, index = 0](const QChar character) mutable {
		       return index++ == separator ? character == '_' : character.isLetterOrNumber();
	       });
}
} // namespace

web_server::web_server(QObject *parent) : QObject(parent)
{
	connect(&server_, &QTcpServer::newConnection, this, [this] {
		while (auto *socket = server_.nextPendingConnection()) {
			connect(socket, &QTcpSocket::readyRead, socket, [this, socket] { respond(socket); });
			connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
		}
	});
}

QString web_server::url(const QString &report_id)
{
	if (!server_.isListening() && !server_.listen(QHostAddress::LocalHost, 0))
		return {};
	QUrl result(QString("http://127.0.0.1:%1/").arg(server_.serverPort()));
	if (!report_id.isEmpty()) {
		QUrlQuery query;
		query.addQueryItem("report", report_id);
		result.setQuery(query);
	}
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
		socket->write(response(web_assets::html(), "text/html"));
	} else if (path == "/assets/recap.css") {
		socket->write(response(web_assets::css(), "text/css"));
	} else if (path == "/assets/recap.js") {
		socket->write(response(web_assets::script(), "application/javascript"));
	} else if (path.startsWith("/assets/ddragon/")) {
		respond_ddragon(socket, path);
		return;
	} else if (path == "/api/game/") {
		socket->write(error_response("A Game ID is required.", 400));
	} else if (path.startsWith("/api/game/")) {
		respond_game(socket, path.mid(QString("/api/game/").size()));
		return;
	} else if (path == "/api/latest" || path.startsWith("/api/report/")) {
		const QString id = path.startsWith("/api/report/") ? path.mid(QString("/api/report/").size())
								   : QString();
		const auto reports = store().reports();
		const auto it = path == "/api/latest"
					? reports.cbegin()
					: std::find_if(reports.cbegin(), reports.cend(), [&id](const auto &value) {
						  return !id.isEmpty() && value.id == id;
					  });
		if (it != reports.cend())
			socket->write(response(QJsonDocument(to_json(*it)).toJson(), "application/json"));
		else
			socket->write(response("Not found", "text/plain", 404));
	} else {
		socket->write(response("Not found", "text/plain", 404));
	}
	socket->disconnectFromHost();
}

void web_server::respond_game(QTcpSocket *socket, const QString &game_id)
{
	if (!valid_game_id(game_id)) {
		socket->write(error_response("Enter a Riot Game ID such as NA1_123456789.", 400));
		socket->disconnectFromHost();
		return;
	}
	const auto reports = store().reports();
	const auto existing = std::find_if(reports.cbegin(), reports.cend(),
					   [&game_id](const auto &value) { return value.game_id == game_id; });
	if (existing != reports.cend()) {
		socket->write(response(QJsonDocument(to_json(*existing)).toJson(), "application/json"));
		socket->disconnectFromHost();
		return;
	}
	if (reports.isEmpty() || reports.first().player.isEmpty()) {
		socket->write(
			error_response("Record a local game first so the plugin can identify your Riot ID.", 400));
		socket->disconnectFromHost();
		return;
	}
	report value;
	value.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	value.game_id = game_id;
	value.player = reports.first().player;
	const QPointer<QTcpSocket> guarded_socket(socket);
	riot_.enrich(std::move(value), [guarded_socket](report loaded, const QString &status) {
		const bool found = loaded.enrichment.value("riot_match_v5").toBool();
		if (found)
			store().save(loaded);
		if (!guarded_socket || guarded_socket->state() != QAbstractSocket::ConnectedState)
			return;
		guarded_socket->write(found ? response(QJsonDocument(to_json(loaded)).toJson(), "application/json")
					    : error_response(status));
		guarded_socket->disconnectFromHost();
	});
}

void web_server::respond_ddragon(QTcpSocket *socket, const QString &path)
{
	const QStringList part = path.split('/', Qt::SkipEmptyParts);
	const bool image = part.size() == 5 && (part[2] == "champion" || part[2] == "item");
	const bool ability = part.size() == 6 && part[2] == "ability" && part[5].size() == 1 &&
			     QString("QWER").contains(part[5]);
	if ((!image && !ability) || part[0] != "assets" || part[1] != "ddragon" || !safe_path_part(part[3]) ||
	    !safe_path_part(part[4])) {
		socket->write(response("Not found", "text/plain", 404));
		socket->disconnectFromHost();
		return;
	}
	const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
			     "/league-game-reports/ddragon/" + part[3];
	const QString directory = root + "/" + part[2];
	const QString file_path = directory + "/" + part[4] + (ability ? "-" + part[5] : "") + ".png";
	QFile cached(file_path);
	if (cached.open(QIODevice::ReadOnly)) {
		socket->write(response(cached.readAll(), "image/png"));
		socket->disconnectFromHost();
		return;
	}
	if (ability) {
		const QString metadata = root + "/champion-" + part[4] + ".json";
		const QPointer<QTcpSocket> guarded_socket(socket);
		auto fetch_icon = [this, guarded_socket, directory, file_path, part](const QJsonObject &champion) {
			const QString image_name = ability_icon_filename(champion, part[4], part[5].front());
			if (image_name.isEmpty()) {
				if (guarded_socket && guarded_socket->state() == QAbstractSocket::ConnectedState) {
					guarded_socket->write(response("Not found", "text/plain", 404));
					guarded_socket->disconnectFromHost();
				}
				return;
			}
			auto *icon = network_.get(
				QNetworkRequest(QUrl(QString("https://ddragon.leagueoflegends.com/cdn/%1/img/spell/%2")
							     .arg(part[3], image_name))));
			connect(icon, &QNetworkReply::finished, icon, [guarded_socket, icon, directory, file_path] {
				const QByteArray bytes = icon->error() == QNetworkReply::NoError ? icon->readAll()
												 : QByteArray{};
				if (!bytes.isEmpty()) {
					QDir().mkpath(directory);
					QSaveFile file(file_path);
					if (file.open(QIODevice::WriteOnly)) {
						file.write(bytes);
						file.commit();
					}
				}
				if (guarded_socket && guarded_socket->state() == QAbstractSocket::ConnectedState) {
					guarded_socket->write(bytes.isEmpty() ? response("Not found", "text/plain", 404)
									      : response(bytes, "image/png"));
					guarded_socket->disconnectFromHost();
				}
				icon->deleteLater();
			});
		};
		QFile metadata_file(metadata);
		if (metadata_file.open(QIODevice::ReadOnly)) {
			fetch_icon(QJsonDocument::fromJson(metadata_file.readAll()).object());
			return;
		}
		auto *reply = network_.get(QNetworkRequest(
			QUrl(QString("https://ddragon.leagueoflegends.com/cdn/%1/data/en_US/champion/%2.json")
				     .arg(part[3], part[4]))));
		connect(reply, &QNetworkReply::finished, reply, [reply, metadata, fetch_icon] {
			const QByteArray bytes = reply->error() == QNetworkReply::NoError ? reply->readAll()
											  : QByteArray{};
			if (!bytes.isEmpty()) {
				QDir().mkpath(QFileInfo(metadata).dir().path());
				QSaveFile file(metadata);
				if (file.open(QIODevice::WriteOnly)) {
					file.write(bytes);
					file.commit();
				}
			}
			fetch_icon(QJsonDocument::fromJson(bytes).object());
			reply->deleteLater();
		});
		return;
	}
	const QString remote =
		part[2] == "champion"
			? QString("https://ddragon.leagueoflegends.com/cdn/%1/img/champion/%2.png").arg(part[3], part[4])
			: QString("https://ddragon.leagueoflegends.com/cdn/%1/img/item/%2.png").arg(part[3], part[4]);
	auto *reply = network_.get(QNetworkRequest(QUrl(remote)));
	const QPointer<QTcpSocket> guarded_socket(socket);
	connect(reply, &QNetworkReply::finished, reply, [guarded_socket, reply, directory, file_path] {
		const QByteArray bytes = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray{};
		if (!bytes.isEmpty()) {
			QDir().mkpath(directory);
			QSaveFile file(file_path);
			if (file.open(QIODevice::WriteOnly)) {
				file.write(bytes);
				file.commit();
			}
		}
		if (guarded_socket && guarded_socket->state() == QAbstractSocket::ConnectedState) {
			guarded_socket->write(bytes.isEmpty() ? response("Not found", "text/plain", 404)
							      : response(bytes, "image/png"));
			guarded_socket->disconnectFromHost();
		}
		reply->deleteLater();
	});
}
} // namespace sources::lol_game_report
