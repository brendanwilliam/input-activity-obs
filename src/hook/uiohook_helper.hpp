/*
 * Local macOS input capture for Input Activity.
 * Derived from input-overlay; see the GPL-2.0 notice in the implementation.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace uiohook {
struct target_display {
	uint32_t id{};
	std::string label;
	int width{};
	int height{};
};
struct target_application {
	std::string id;
	std::string label;
};
struct target_window {
	std::string application_id;
	uint64_t id{};
	std::string label;
};

struct input_context {
	std::string application_id;
	uint64_t window_id{};
	uint32_t focused_display_id{};

	bool operator!=(const input_context &other) const
	{
		return application_id != other.application_id || window_id != other.window_id ||
		       focused_display_id != other.focused_display_id;
	}
};

void start();
void stop();
std::vector<target_display> target_displays();
std::vector<target_application> target_applications();
std::vector<target_window> target_windows();
input_context current_input_context();
uint32_t display_at(int x, int y);
} // namespace uiohook
