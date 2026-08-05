/*************************************************************************
 * This file is part of input-overlay
 * git.vrsal.xyz/alex/input-overlay
 * Copyright 2023 Alex <uni@vrsal.xyz>.
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

#include "input/input_data.hpp"
#include <util/platform.h>

#include <utility>

namespace local_data {
input_data data;
}

void input_data::copy(const input_data *other)
{
	last_event.store(other->last_event);
	keyboard = other->keyboard;
	mouse = other->mouse;
	last_mouse_movement = other->last_mouse_movement;
	last_wheel_event_time = other->last_wheel_event_time;
	last_wheel_event = other->last_wheel_event;
	last_event_type.store(other->last_event_type);
	if (other->trace.empty()) {
		trace_sequence = other->trace_sequence;
		return;
	}
	const uint64_t oldest = other->trace.front().sequence;
	if (trace_sequence + 1 < oldest) {
		trace.clear();
		trace_sequence = oldest - 1;
	}
	const size_t first_new = trace_sequence < oldest ? 0
							 : std::min(other->trace.size(),
								    static_cast<size_t>(trace_sequence - oldest + 1));
	for (size_t index = first_new; index < other->trace.size(); ++index) {
		trace.push_back(other->trace[index]);
		if (trace.size() > trace_capacity)
			trace.pop_front();
	}
	trace_sequence = other->trace_sequence;
}

void input_data::dispatch_uiohook_event(const uiohook_event *event, trace_event context)
{
	trace_event trace_entry = std::move(context);
	trace_entry.sequence = ++trace_sequence;
	trace_entry.time_ns = os_gettime_ns();
	trace_entry.type = event->type;
	if (event->type == EVENT_KEY_PRESSED || event->type == EVENT_KEY_RELEASED) {
		trace_entry.code = event->data.keyboard.keycode;
		trace_entry.keychar = event->data.keyboard.keychar;
	} else if (event->type >= EVENT_MOUSE_CLICKED) {
		trace_entry.code = event->data.mouse.button;
		trace_entry.x = event->data.mouse.x;
		trace_entry.y = event->data.mouse.y;
	}
	trace.push_back(trace_entry);
	if (trace.size() > trace_capacity)
		trace.pop_front();
	if (event->type == EVENT_MOUSE_WHEEL) {
		last_wheel_event = event->data.wheel;
		last_wheel_event_time = os_gettime_ns();
		last_event = event->time;
	} else if (event->type == EVENT_MOUSE_DRAGGED || event->type == EVENT_MOUSE_MOVED) {
		last_mouse_movement = event->data.mouse;
		last_event = event->time;
	} else if (event->type == EVENT_KEY_PRESSED || event->type == EVENT_KEY_RELEASED) {
		keyboard[event->data.keyboard.keycode] = event->type == EVENT_KEY_PRESSED;
		last_event = event->time;
	} else if (event->type == EVENT_MOUSE_PRESSED || event->type == EVENT_MOUSE_RELEASED) {
		last_event = event->time;
		mouse[event->data.mouse.button] = event->type == EVENT_MOUSE_PRESSED;
	}
	last_event_type = event->type;
}

void input_data::events_after(uint64_t &cursor, std::vector<trace_event> &out) const
{
	out.clear();
	if (trace.empty())
		return;
	const uint64_t oldest = trace.front().sequence;
	if (cursor + 1 < oldest)
		cursor = oldest - 1;
	const size_t first_new = static_cast<size_t>(cursor - oldest + 1);
	out.assign(trace.begin() + static_cast<std::ptrdiff_t>(first_new), trace.end());
	cursor = trace.back().sequence;
}
