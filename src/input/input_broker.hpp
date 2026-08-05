#pragma once

#include "input/input_data.hpp"

#include <uiohook.h>

#include <cstdint>
#include <string>
#include <vector>

namespace input_broker {
enum class target_type { all, display, application, window };

struct target {
	target_type type{target_type::all};
	uint32_t display{};
	bool rectangle_enabled{};
	int rectangle_left{}, rectangle_top{}, rectangle_right{}, rectangle_bottom{};
	std::string application_id;
	uint64_t window_id{};

	bool operator==(const target &other) const;
};

void push(const uiohook_event *event);
void consume(const target &target, uint64_t &cursor, bool &discard_backlog,
	     std::vector<input_data::trace_event> &events, input_data::button_map<uint16_t> &keyboard,
	     input_data::button_map<uint16_t> &mouse);
} // namespace input_broker
