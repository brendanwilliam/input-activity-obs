#pragma once

#include <QJsonObject>
#include <QString>

namespace sources::lol_game_report {

QString ability_icon_filename(const QJsonObject &metadata, const QString &champion_id, QChar slot);

} // namespace sources::lol_game_report
