#pragma once

#include "lol_game_report_types.hpp"

#include <QString>
#include <QVector>

namespace sources::lol_game_report {

class store {
public:
	store();
	QVector<report> reports() const;
	bool save(report value);
	bool remove(const QString &id);
	bool export_report(const report &value, const QString &directory, const QByteArray &png) const;
	QString directory() const;

private:
	bool write_index(const QVector<QString> &ids) const;
	QVector<QString> index() const;
	QString report_path(const QString &id) const;
	QString root_;
};

} // namespace sources::lol_game_report
