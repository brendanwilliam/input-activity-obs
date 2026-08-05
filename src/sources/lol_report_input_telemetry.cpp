#include "lol_report_input_telemetry.hpp"

#include <algorithm>
#include <cmath>

namespace sources::lol_game_report {
namespace {
constexpr uint64_t second_ns = 1000000000ULL;
} // namespace

void input_telemetry::set_game_frame(const QRect &frame)
{
	game_frame_ = frame.isValid() && !frame.isEmpty() ? frame : QRect(0, 0, 1920, 1080);
	hex_grid_.frame_aspect_ratio = double(game_frame_.width()) / std::max(1, game_frame_.height());
}

void input_telemetry::set_hex_radius_percent(int radius_percent)
{
	hex_grid_.radius_percent = std::clamp(radius_percent, 1, 20);
}

void input_telemetry::reset()
{
	last_motion_.reset();
	last_motion_time_ns_ = 0;
	session_start_ns_ = 0;
	session_start_seconds_ = 0;
	held_keys_.clear();
}

input_sample &input_telemetry::sample_for(uint64_t time_ns, int game_seconds, QVector<input_sample> &samples)
{
	if (!session_start_ns_) {
		session_start_ns_ = time_ns;
		session_start_seconds_ = std::max(0, game_seconds);
	}
	const int seconds = session_start_seconds_ + int((time_ns - session_start_ns_) / second_ns);
	if (samples.isEmpty()) {
		samples.append({seconds});
	} else {
		for (int missing = samples.last().seconds + 1; missing <= seconds; ++missing)
			samples.append({missing});
	}
	return samples.last();
}

QPointF input_telemetry::canonical_point(const QPoint &point) const
{
	const double width = std::max(1, game_frame_.width());
	return {(point.x() - game_frame_.left()) * 100.0 / width, (point.y() - game_frame_.top()) * 100.0 / width};
}

void input_telemetry::consume(const std::vector<input_data::trace_event> &events, int game_seconds,
			      QVector<input_sample> &samples, QVector<hexbin> &hexbins)
{
	for (const auto &event : events) {
		auto &sample = sample_for(event.time_ns, game_seconds, samples);
		if (event.type == EVENT_KEY_PRESSED && !held_keys_[event.code]) {
			held_keys_[event.code] = true;
			++sample.actions;
		} else if (event.type == EVENT_KEY_RELEASED) {
			held_keys_[event.code] = false;
		} else if (event.type == EVENT_MOUSE_PRESSED) {
			++sample.actions;
		}
		if (event.type != EVENT_MOUSE_MOVED && event.type != EVENT_MOUSE_DRAGGED)
			continue;
		const QPoint point(event.x, event.y);
		if (!game_frame_.contains(point)) {
			last_motion_.reset();
			last_motion_time_ns_ = 0;
			continue;
		}
		if (last_motion_) {
			const double distance =
				std::hypot(point.x() - last_motion_->x(), point.y() - last_motion_->y());
			sample.mouse_distance_pixels += distance;
			if (event.time_ns > last_motion_time_ns_) {
				const uint64_t elapsed = event.time_ns - last_motion_time_ns_;
				add_hex_dwell(hexbins, nearest_hex(hex_grid_, canonical_point(*last_motion_)),
					      std::min(elapsed / 1000000ULL, dwell_gap_limit_ms));
				if (elapsed)
					sample.max_velocity_pixels_per_second = std::max(
						sample.max_velocity_pixels_per_second, distance * second_ns / elapsed);
			}
		}
		last_motion_ = point;
		last_motion_time_ns_ = event.time_ns;
	}
}

} // namespace sources::lol_game_report
