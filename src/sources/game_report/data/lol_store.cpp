#include "lol_store.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

namespace sources::lol_game_report {
store::store()
{
	root_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/league-game-reports";
	QDir().mkpath(root_);
}

QString store::directory() const
{
	return root_;
}
QString store::report_path(const QString &id) const
{
	return root_ + "/" + id + ".json";
}

QVector<QString> store::index() const
{
	QFile file(root_ + "/index.json");
	if (!file.open(QIODevice::ReadOnly))
		return {};
	QVector<QString> ids;
	const auto reports = QJsonDocument::fromJson(file.readAll()).object()["reports"].toArray();
	for (const QJsonValue value : reports)
		if (!value.toString().isEmpty())
			ids.append(value.toString());
	return ids;
}

bool store::write_index(const QVector<QString> &ids) const
{
	QJsonArray reports;
	for (const auto &id : ids)
		reports.append(id);
	QSaveFile file(root_ + "/index.json");
	if (!file.open(QIODevice::WriteOnly))
		return false;
	file.write(
		QJsonDocument(QJsonObject{{"schema_version", 1}, {"reports", reports}}).toJson(QJsonDocument::Compact));
	return file.commit();
}

QVector<report> store::reports() const
{
	QVector<report> result;
	for (const auto &id : index()) {
		QFile file(report_path(id));
		report value;
		if (file.open(QIODevice::ReadOnly) &&
		    from_json(QJsonDocument::fromJson(file.readAll()).object(), value))
			result.append(value);
	}
	return result;
}

bool store::save(report value)
{
	if (value.id.isEmpty())
		return false;
	QSaveFile file(report_path(value.id));
	if (!file.open(QIODevice::WriteOnly))
		return false;
	file.write(QJsonDocument(to_json(value)).toJson(QJsonDocument::Indented));
	if (!file.commit())
		return false;
	QVector<QString> ids = index();
	ids.removeAll(value.id);
	ids.prepend(value.id);
	const QVector<QString> removed = ids.mid(20);
	ids.resize(std::min<int>(20, ids.size()));
	if (!write_index(ids))
		return false;
	for (const auto &id : removed)
		QFile::remove(report_path(id));
	return true;
}

bool store::remove(const QString &id)
{
	QVector<QString> ids = index();
	if (!ids.removeOne(id))
		return false;
	if (!write_index(ids))
		return false;
	return QFile::remove(report_path(id));
}

bool store::export_report(const report &value, const QString &directory, const QByteArray &png) const
{
	QDir target(directory);
	if (!target.exists() && !target.mkpath("."))
		return false;
	const QString base = target.filePath("league-game-report-" + value.id);
	QSaveFile json(base + ".json");
	QSaveFile image(base + ".png");
	if (!json.open(QIODevice::WriteOnly) || !image.open(QIODevice::WriteOnly))
		return false;
	json.write(QJsonDocument(to_json(value)).toJson(QJsonDocument::Indented));
	image.write(png);
	return json.commit() && image.commit();
}
} // namespace sources::lol_game_report
