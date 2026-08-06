struct shared_collector {
	std::mutex mutex;
	int references{};
	int development_log_references{};
	std::atomic<collection_state> state{collection_state::empty};
	QThread thread;
	worker *worker{};
	void acquire()
	{
		std::lock_guard lock(mutex);
		if (references++ != 0)
			return;
		worker = new class worker(state);
		worker->moveToThread(&thread);
		QObject::connect(&thread, &QThread::started, worker, [this] { worker->start(); });
		thread.start();
	}
	void release()
	{
		std::lock_guard lock(mutex);
		if (references <= 0)
			return;
		if (--references != 0)
			return;
		QMetaObject::invokeMethod(worker, [this] { worker->stop(); }, Qt::BlockingQueuedConnection);
		thread.quit();
		thread.wait();
		delete worker;
		worker = nullptr;
		state = collection_state::empty;
	}
	void set_dpi(int dpi)
	{
		QMetaObject::invokeMethod(worker, [this, dpi] { worker->set_dpi(dpi); }, Qt::QueuedConnection);
	}
	void set_hex_radius_percent(double radius_percent)
	{
		QMetaObject::invokeMethod(
			worker, [this, radius_percent] { worker->set_hex_radius_percent(radius_percent); },
			Qt::QueuedConnection);
	}
	void set_game_frame(const QRect &frame)
	{
		QMetaObject::invokeMethod(
			worker, [this, frame] { worker->set_game_frame(frame); }, Qt::QueuedConnection);
	}
	void consume_input(const std::vector<input_data::trace_event> &events)
	{
		if (!events.empty())
			QMetaObject::invokeMethod(
				worker, [this, events] { worker->consume_input(events); }, Qt::QueuedConnection);
	}
	void set_auto_open(bool enabled)
	{
		QMetaObject::invokeMethod(
			worker, [this, enabled] { worker->set_auto_open(enabled); }, Qt::QueuedConnection);
	}
	void set_development_logs(bool enabled)
	{
		std::lock_guard lock(mutex);
		if (enabled)
			++development_log_references;
		else if (development_log_references > 0)
			--development_log_references;
		if (!worker)
			return;
		const bool active = development_log_references > 0;
		QMetaObject::invokeMethod(
			worker, [this, active] { worker->set_development_logs(active); }, Qt::QueuedConnection);
	}
	bool development_logs_enabled()
	{
		std::lock_guard lock(mutex);
		return development_log_references > 0;
	}
	QString development_log_path()
	{
		QString result;
		std::lock_guard lock(mutex);
		if (worker)
			QMetaObject::invokeMethod(
				worker, [this, &result] { result = worker->development_log_path(); },
				Qt::BlockingQueuedConnection);
		return result;
	}
	void log_riot_diagnostic(const QJsonObject &fields)
	{
		std::lock_guard lock(mutex);
		if (worker)
			QMetaObject::invokeMethod(
				worker, [this, fields] { worker->log_riot_diagnostic(fields); }, Qt::QueuedConnection);
	}
	QString recap_url()
	{
		QString result;
		QMetaObject::invokeMethod(
			worker, [this, &result] { result = worker->recap_url(); }, Qt::BlockingQueuedConnection);
		return result;
	}
};
shared_collector &shared()
{
	static shared_collector value;
	return value;
}
} // namespace

collector::collector()
{
	shared().acquire();
}
collector::~collector()
{
	if (development_logs_)
		shared().set_development_logs(false);
	shared().release();
}
collection_state collector::state() const
{
	return shared().state.load();
}
void collector::set_dpi(int dpi)
{
	shared().set_dpi(dpi);
}
void collector::set_hex_radius_percent(double radius_percent)
{
	shared().set_hex_radius_percent(radius_percent);
}
void collector::set_game_frame(const QRect &frame)
{
	shared().set_game_frame(frame);
}
void collector::tick(int dpi, double hex_radius_percent)
{
	set_dpi(dpi);
	set_hex_radius_percent(hex_radius_percent);
	if (!uiohook::league_game_is_frontmost()) {
		discard_backlog_ = true;
		return;
	}
	std::vector<input_data::trace_event> events;
	input_data::button_map<uint16_t> keyboard, mouse;
	input_broker::consume({}, cursor_, discard_backlog_, events, keyboard, mouse);
	// The broker carries key characters for live-key displays. Reports retain only action counts.
	for (auto &event : events)
		event.keychar = 0;
	shared().consume_input(events);
}
void collector::set_auto_open(bool enabled)
{
	shared().set_auto_open(enabled);
}
void collector::set_development_logs(bool enabled)
{
	if (development_logs_ == enabled)
		return;
	development_logs_ = enabled;
	shared().set_development_logs(enabled);
}
bool collector::development_logs_enabled() const
{
	return shared().development_logs_enabled();
}
QString collector::development_log_path() const
{
	return shared().development_log_path();
}
void collector::log_riot_diagnostic(const QJsonObject &fields)
{
	shared().log_riot_diagnostic(fields);
}
QString collector::recap_url() const
{
	return shared().recap_url();
}
QString collector::state_text(collection_state value)
{
	if (value == collection_state::recording)
		return "Recording self-only local game data";
	if (value == collection_state::finalizing)
		return "Finalizing report";
	return "Waiting for a local League game";
}
