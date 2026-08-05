#include "input/input_broker.hpp"

#include "hook/uiohook_helper.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <unordered_map>
#include <uiohook.h>
#include <util/platform.h>

namespace input_broker {
namespace {
constexpr size_t raw_capacity = 8192;
constexpr size_t routed_capacity = 4096;

struct raw_event {
	uint64_t sequence{};
	uint64_t time_ns{};
	uint16_t type{};
	uint16_t code{};
	int16_t x{};
	int16_t y{};
	wchar_t keychar{};
};

struct target_hash {
	size_t operator()(const target &value) const
	{
		size_t result = std::hash<int>{}(static_cast<int>(value.type));
		const auto combine = [&result](size_t next) {
			result ^= next + 0x9e3779b9 + (result << 6) + (result >> 2);
		};
		combine(std::hash<uint32_t>{}(value.display));
		combine(std::hash<bool>{}(value.rectangle_enabled));
		combine(std::hash<int>{}(value.rectangle_left));
		combine(std::hash<int>{}(value.rectangle_top));
		combine(std::hash<int>{}(value.rectangle_right));
		combine(std::hash<int>{}(value.rectangle_bottom));
		combine(std::hash<std::string>{}(value.application_id));
		combine(std::hash<uint64_t>{}(value.window_id));
		return result;
	}
};

struct routed_group {
	std::deque<input_data::trace_event> events;
	uint64_t latest_sequence{};
};

bool is_motion(uint16_t type)
{
	return type == EVENT_MOUSE_MOVED || type == EVENT_MOUSE_DRAGGED;
}

class broker {
public:
	void push(const uiohook_event *event)
	{
		const size_t write = write_index.load(std::memory_order_relaxed);
		const size_t next = (write + 1) % raw_capacity;
		if (next == read_index.load(std::memory_order_acquire)) {
			dropped.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		raw_event raw{};
		raw.sequence = produced.fetch_add(1, std::memory_order_relaxed) + 1;
		raw.time_ns = os_gettime_ns();
		raw.type = event->type;
		if (event->type == EVENT_KEY_PRESSED || event->type == EVENT_KEY_RELEASED) {
			raw.code = event->data.keyboard.keycode;
			raw.keychar = event->data.keyboard.keychar;
		} else if (event->type >= EVENT_MOUSE_CLICKED) {
			raw.code = event->data.mouse.button;
			raw.x = event->data.mouse.x;
			raw.y = event->data.mouse.y;
		}
		raw_events[write] = raw;
		write_index.store(next, std::memory_order_release);
	}

	void consume(const target &key, uint64_t &cursor, bool &discard_backlog,
		     std::vector<input_data::trace_event> &events, input_data::button_map<uint16_t> &keyboard_out,
		     input_data::button_map<uint16_t> &mouse_out)
	{
		auto &group = groups[key];
		drain();
		if (discard_backlog) {
			cursor = group.latest_sequence;
			discard_backlog = false;
		}
		events.clear();
		if (!group.events.empty()) {
			const uint64_t oldest = group.events.front().sequence;
			if (cursor < oldest)
				cursor = oldest - 1;
			if (cursor < group.latest_sequence) {
				const size_t first = static_cast<size_t>(cursor - oldest + 1);
				events.assign(group.events.begin() + static_cast<std::ptrdiff_t>(first),
					      group.events.end());
			}
			cursor = group.latest_sequence;
		}
		keyboard_out = keyboard;
		mouse_out = mouse;
	}

private:
	bool pop(raw_event &event)
	{
		const size_t read = read_index.load(std::memory_order_relaxed);
		if (read == write_index.load(std::memory_order_acquire))
			return false;
		event = raw_events[read];
		read_index.store((read + 1) % raw_capacity, std::memory_order_release);
		return true;
	}

	void drain()
	{
		raw_event event{};
		if (!pop(event))
			return;
		if (dropped.exchange(0, std::memory_order_acq_rel) != 0) {
			keyboard.clear();
			mouse.clear();
		}
		const uiohook::input_context context = uiohook::current_input_context();
		const bool context_changed = has_context && context != last_context;
		last_context = context;
		has_context = true;
		route(event, context, context_changed);
		while (pop(event))
			route(event, context, context_changed);
	}

	void route(const raw_event &raw, const uiohook::input_context &context, bool context_changed)
	{
		if (raw.type == EVENT_KEY_PRESSED || raw.type == EVENT_KEY_RELEASED)
			keyboard[raw.code] = raw.type == EVENT_KEY_PRESSED;
		if (raw.type == EVENT_MOUSE_PRESSED || raw.type == EVENT_MOUSE_RELEASED)
			mouse[raw.code] = raw.type == EVENT_MOUSE_PRESSED;
		uint32_t pointer_display{};
		for (auto &[key, group] : groups) {
			if (!matches(key, raw, context, context_changed, pointer_display))
				continue;
			input_data::trace_event event{};
			event.sequence = ++group.latest_sequence;
			event.time_ns = raw.time_ns;
			event.type = raw.type;
			event.code = raw.code;
			event.x = raw.x;
			event.y = raw.y;
			event.keychar = raw.keychar;
			event.application_id = context.application_id;
			event.window_id = context.window_id;
			event.focused_display_id = context.focused_display_id;
			event.pointer_display_id = pointer_display;
			group.events.push_back(std::move(event));
			if (group.events.size() > routed_capacity)
				group.events.pop_front();
		}
	}

	bool matches(const target &key, const raw_event &raw, const uiohook::input_context &context,
		     bool context_changed, uint32_t &pointer_display) const
	{
		switch (key.type) {
		case target_type::all:
			return true;
		case target_type::display: {
			const bool motion = is_motion(raw.type);
			if (motion && pointer_display == 0)
				pointer_display = uiohook::display_at(raw.x, raw.y);
			if ((motion ? pointer_display : context.focused_display_id) != key.display)
				return false;
			return !key.rectangle_enabled || !motion ||
			       (raw.x >= key.rectangle_left && raw.x <= key.rectangle_right &&
				raw.y >= key.rectangle_top && raw.y <= key.rectangle_bottom);
		}
		case target_type::application:
			return !context_changed && !key.application_id.empty() &&
			       context.application_id == key.application_id;
		case target_type::window:
			return !context_changed && key.window_id != 0 && context.application_id == key.application_id &&
			       context.window_id == key.window_id;
		}
		return false;
	}

	std::array<raw_event, raw_capacity> raw_events{};
	std::atomic<size_t> write_index{};
	std::atomic<size_t> read_index{};
	std::atomic<uint64_t> produced{};
	std::atomic<uint64_t> dropped{};
	std::unordered_map<target, routed_group, target_hash> groups;
	input_data::button_map<uint16_t> keyboard;
	input_data::button_map<uint16_t> mouse;
	uiohook::input_context last_context;
	bool has_context{};
};

broker &instance()
{
	static broker value;
	return value;
}
} // namespace

bool target::operator==(const target &other) const
{
	return type == other.type && display == other.display && rectangle_enabled == other.rectangle_enabled &&
	       rectangle_left == other.rectangle_left && rectangle_top == other.rectangle_top &&
	       rectangle_right == other.rectangle_right && rectangle_bottom == other.rectangle_bottom &&
	       application_id == other.application_id && window_id == other.window_id;
}

void push(const uiohook_event *event)
{
	instance().push(event);
}

void consume(const target &target, uint64_t &cursor, bool &discard_backlog,
	     std::vector<input_data::trace_event> &events, input_data::button_map<uint16_t> &keyboard,
	     input_data::button_map<uint16_t> &mouse)
{
	instance().consume(target, cursor, discard_backlog, events, keyboard, mouse);
}
} // namespace input_broker
