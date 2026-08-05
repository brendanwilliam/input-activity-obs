#include "lol_game_report_ddragon.hpp"

#include <QJsonArray>

namespace sources::lol_game_report {
namespace {
bool safe_filename(const QString &value)
{
	return !value.isEmpty() && value.size() < 96 && !value.contains("..") && !value.contains('/');
}
} // namespace

QString ability_icon_filename(const QJsonObject &metadata, const QString &champion_id, QChar slot)
{
	const qsizetype slot_index = QString("QWER").indexOf(slot);
	if (champion_id.isEmpty() || slot_index < 0)
		return {};

	const QJsonObject data = metadata.value("data").toObject();
	const QJsonObject champion = data.value(champion_id).toObject();
	const QJsonArray spells = champion.value("spells").toArray();
	if (champion.isEmpty() || slot_index >= spells.size())
		return {};

	const QJsonObject spell = spells.at(slot_index).toObject();
	const QJsonObject image = spell.value("image").toObject();
	const QString filename = image.value("full").toString();
	return safe_filename(filename) ? filename : QString{};
}
} // namespace sources::lol_game_report
