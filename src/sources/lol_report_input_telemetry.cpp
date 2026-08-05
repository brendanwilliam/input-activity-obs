#include "lol_report_input_telemetry.hpp"

#include <algorithm>
#include <cmath>

namespace sources::lol_game_report {
namespace {
constexpr int heatmap_columns = 30;
constexpr int heatmap_rows = 17;
constexpr uint64_t second_ns = 1000000000ULL;
} // namespace

void input_telemetry::set_game_frame(const QRect &frame)
{
	game_frame_ = frame.isValid() && !frame.isEmpty() ? frame : QRect(0, 0, 1920, 1080);
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

void input_telemetry::add_heatmap_point(const QPoint &point, QVector<heatmap_bin> &heatmap) const
{
	const int x = std::clamp((point.x() - game_frame_.left()) * heatmap_columns / std::max(1, game_frame_.width()),
				 0, heatmap_columns - 1);
	const int y = std::clamp((point.y() - game_frame_.top()) * heatmap_rows / std::max(1, game_frame_.height()), 0,
				 heatmap_rows - 1);
	auto bin = std::find_if(heatmap.begin(), heatmap.end(),
				[x, y](const auto &value) { return value.x == x && value.y == y; });
	if (bin == heatmap.end())
		heatmap.append({x, y, 1});
	else
		++bin->count;
}

void input_telemetry::consume(const std::vector<input_data::trace_event> &events, int game_seconds,
			      QVector<input_sample> &samples, QVector<heatmap_bin> &heatmap)
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
				if (elapsed)
					sample.max_velocity_pixels_per_second = std::max(
						sample.max_velocity_pixels_per_second, distance * second_ns / elapsed);
			}
		}
		last_motion_ = point;
		last_motion_time_ns_ = event.time_ns;
		add_heatmap_point(point, heatmap);
	}
}

} // namespace sources::lol_game_report
