#pragma once

#include "lol_game_report_types.hpp"

#include <QObject>

#include <functional>

class QNetworkAccessManager;

namespace sources::lol_game_report {

class riot_api final : public QObject {
public:
	explicit riot_api(QObject *parent = nullptr);
	void enrich(report value, std::function<void(report, QString)> complete);

private:
	QNetworkAccessManager *manager_{};
};

} // namespace sources::lol_game_report
