#pragma once

#include "lol_game_report_types.hpp"

#include "../../input/input_data.hpp"

#include <QRect>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace sources::lol_game_report {

class input_telemetry {
public:
	void set_game_frame(const QRect &frame);
	void set_hex_radius_percent(int radius_percent);
	void reset();
	void consume(const std::vector<input_data::trace_event> &events, int game_seconds,
		     QVector<input_sample> &samples, QVector<hexbin> &hexbins);

private:
	input_sample &sample_for(uint64_t time_ns, int game_seconds, QVector<input_sample> &samples);
	QPointF canonical_point(const QPoint &point) const;

	QRect game_frame_{0, 0, 1920, 1080};
	hex_grid hex_grid_;
	std::optional<QPoint> last_motion_;
	uint64_t last_motion_time_ns_{};
	uint64_t session_start_ns_{};
	int session_start_seconds_{};
	std::unordered_map<uint16_t, bool> held_keys_;
};

} // namespace sources::lol_game_report
