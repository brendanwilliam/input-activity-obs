#pragma once

#include <QPointF>
#include <QVector>

namespace sources::lol_heatmap {

constexpr double default_radius_percent = 4.0;

struct grid {
	double frame_aspect_ratio{16.0 / 9.0};
	double radius_percent{default_radius_percent};
};

struct cell {
	int column{};
	int row{};
};

double canonical_height(const grid &value);
QPointF center(const grid &value, int column, int row);
cell nearest_cell(const grid &value, const QPointF &point);
QVector<cell> visible_cells(const grid &value);

} // namespace sources::lol_heatmap
