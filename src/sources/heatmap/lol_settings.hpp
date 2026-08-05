#pragma once

#include "sources/heatmap/lol_geometry.hpp"

namespace sources::lol_heatmap {

double radius_percent();
void set_radius_percent(double value);
void migrate_legacy_radius(int legacy_radius_pixels, int content_width_pixels);

} // namespace sources::lol_heatmap
