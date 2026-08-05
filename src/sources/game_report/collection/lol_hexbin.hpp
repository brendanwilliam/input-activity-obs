#pragma once

#include "sources/heatmap/lol_geometry.hpp"

#include <QPointF>
#include <QVector>

#include <cstdint>

namespace sources::lol_game_report {

struct hexbin {
	int column{};
	int row{};
	uint64_t dwell_ms{};
};

constexpr double default_hex_radius_percent = lol_heatmap::default_radius_percent;
constexpr uint64_t dwell_gap_limit_ms = 250;

using hex_grid = lol_heatmap::grid;

double canonical_height(const hex_grid &grid);
QPointF hex_center(const hex_grid &grid, int column, int row);
hexbin nearest_hex(const hex_grid &grid, const QPointF &point);
void add_hex_dwell(QVector<hexbin> &bins, const hexbin &bin, uint64_t dwell_ms);

} // namespace sources::lol_game_report
