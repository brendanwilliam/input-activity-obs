#pragma once

#include "sources/game_report/data/lol_types.hpp"

#include <QObject>

class QNetworkAccessManager;

namespace sources::lol_game_report {

class online_reports final : public QObject {
public:
	explicit online_reports(QObject *parent = nullptr);
	~online_reports() override;
	void observe(const QVector<report> &reports);
	void tick();
	void set_service_url(const QString &value);
	void begin_link();
	void unlink();
	void retry();
	QString status() const;
	bool linked() const;

private:
	void save_queue() const;
	void load_queue();
	void poll_device_code();
	QString credential() const;
	bool save_credential(const QString &value) const;
	void clear_credential() const;

	class implementation;
	implementation *implementation_;
};

} // namespace sources::lol_game_report
