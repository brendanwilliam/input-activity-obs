#pragma once

#include <QPointF>
#include <QVector>

#include <cstdint>

namespace sources::lol_game_report {

struct hexbin {
	int column{};
	int row{};
	uint64_t dwell_ms{};
};

constexpr int default_hex_radius_percent = 4;
constexpr uint64_t dwell_gap_limit_ms = 250;

struct hex_grid {
	double frame_aspect_ratio{16.0 / 9.0};
	int radius_percent{default_hex_radius_percent};
};

double canonical_height(const hex_grid &grid);
QPointF hex_center(const hex_grid &grid, int column, int row);
hexbin nearest_hex(const hex_grid &grid, const QPointF &point);
void add_hex_dwell(QVector<hexbin> &bins, const hexbin &bin, uint64_t dwell_ms);

} // namespace sources::lol_game_report
