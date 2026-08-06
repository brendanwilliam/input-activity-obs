#pragma once

#include <QRect>

struct obs_data;
struct obs_properties;

namespace sources {
class lol_report_manager {
public:
	lol_report_manager();
	~lol_report_manager();
	lol_report_manager(const lol_report_manager &) = delete;
	void update(obs_data *settings);
	void tick(const QRect &game_frame, double hex_radius_percent);
	bool export_selected();
	bool delete_selected();
	bool enrich_selected();
	bool reveal_development_log() const;
	void add_properties(obs_properties *properties);
	static void defaults(obs_data *settings);

private:
	class implementation;
	implementation *implementation_;
};
} // namespace sources
