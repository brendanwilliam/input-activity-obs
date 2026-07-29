/*************************************************************************
 * This file is part of input-overlay
 * git.vrsal.cc/alex/input-overlay
 * Copyright 2025 Alex <uni@vrsal.xyz>.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *************************************************************************/

#pragma once

#include <unordered_map>
#include <atomic>
#include <mutex>
#include <array>
#include <deque>
#include <string>
#include <vector>
#include <uiohook.h>

class QJsonObject;

/* Holds all input data for a computer, local or remote */
struct input_data {
	template<class T> using button_map = std::unordered_map<T, bool>;

	std::mutex m_mutex;

	std::atomic<uint64_t> last_event = 0;
	std::atomic<uint64_t> last_event_type = 0;

	/* A bounded, sequenced event stream for sources that cannot rely on a
     * frame-rate-limited state snapshot (statistics and motion visualizers). */
	struct trace_event {
		uint64_t sequence = 0;
		uint64_t time_ns = 0;
		uint16_t type = 0;
		uint16_t code = 0;
		int16_t x = 0;
		int16_t y = 0;
		wchar_t keychar = 0;
		std::string application_id;
		uint64_t window_id = 0;
		uint32_t focused_display_id = 0;
		uint32_t pointer_display_id = 0;
	};
	static constexpr size_t trace_capacity = 4096;
	uint64_t trace_sequence = 0;
	std::deque<trace_event> trace{};

	/* State of all keyboard keys*/
	button_map<uint16_t> keyboard{};

	/* State of all mouse buttons */
	button_map<uint16_t> mouse{};

	mouse_wheel_event_data last_wheel_event{};
	/* we use this to reset the scroll wheel after a time out */
	uint64_t last_wheel_event_time{};

	/* used for the mouse motion event */
	mouse_event_data last_mouse_movement{};

	/* Mutex needs to be locked */
	void copy(const input_data *other);

	void dispatch_uiohook_event(const uiohook_event *event, trace_event context);

	/* Mutex needs to be locked by callers when this is local_data::data. */
	void events_after(uint64_t &cursor, std::vector<trace_event> &out) const;
};

namespace local_data {
extern input_data data;
}
