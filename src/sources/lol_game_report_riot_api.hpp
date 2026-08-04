#pragma once

#include "lol_game_report_types.hpp"

#include <QObject>
#include <QJsonObject>

#include <functional>

class QNetworkAccessManager;

namespace sources::lol_game_report {

class riot_api final : public QObject {
public:
	explicit riot_api(QObject *parent = nullptr);
	void enrich(report value, std::function<void(report, QString)> complete);
	void enrich_latest(report value, std::function<void(report, QString)> complete);
	void set_diagnostics(std::function<void(const QJsonObject &)> callback) { diagnostics_ = std::move(callback); }

private:
	QNetworkAccessManager *manager_{};
	std::function<void(const QJsonObject &)> diagnostics_;
};

} // namespace sources::lol_game_report
