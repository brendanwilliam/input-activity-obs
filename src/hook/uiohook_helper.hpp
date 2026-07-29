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

void start();
void stop();
std::vector<target_display> target_displays();
std::vector<target_application> target_applications();
std::vector<target_window> target_windows();
} // namespace uiohook
