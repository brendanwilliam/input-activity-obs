#include "sources/heatmap/lol_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sources::lol_heatmap {
namespace {
constexpr double root_three = 1.7320508075688772935;

double radius(const grid &value)
{
	return std::clamp(value.radius_percent, 0.1, 100.0);
}
} // namespace

double canonical_height(const grid &value)
{
	return 100.0 / std::max(0.01, value.frame_aspect_ratio);
}

QPointF center(const grid &value, int column, int row)
{
	const double hex_radius = radius(value);
	return {root_three * hex_radius * (column + (row & 1 ? 0.5 : 0.0)), hex_radius * (1.0 + 1.5 * row)};
}

cell nearest_cell(const grid &value, const QPointF &point)
{
	const double hex_radius = radius(value);
	const int approximate_row = int(std::floor((point.y() / hex_radius - 1.0) / 1.5 + 0.5));
	cell result{};
	double best_distance = std::numeric_limits<double>::infinity();
	for (int row = approximate_row - 2; row <= approximate_row + 2; ++row) {
		const double offset = row & 1 ? 0.5 : 0.0;
		const int approximate_column = int(std::floor(point.x() / (root_three * hex_radius) - offset + 0.5));
		for (int column = approximate_column - 2; column <= approximate_column + 2; ++column) {
			const QPointF candidate = center(value, column, row);
			const double distance = std::hypot(point.x() - candidate.x(), point.y() - candidate.y());
			if (distance < best_distance ||
			    (distance == best_distance &&
			     (row < result.row || (row == result.row && column < result.column)))) {
				result = {column, row};
				best_distance = distance;
			}
		}
	}
	return result;
}

QVector<cell> visible_cells(const grid &value)
{
	const double hex_radius = radius(value);
	const int columns = std::max(1, int(std::ceil(100.0 / (root_three * hex_radius))) + 1);
	const int rows = std::max(1, int(std::ceil(canonical_height(value) / (1.5 * hex_radius))) + 1);
	QVector<cell> result;
	result.reserve(columns * rows);
	for (int row = 0; row < rows; ++row)
		for (int column = 0; column < columns; ++column)
			result.append({column, row});
	return result;
}

} // namespace sources::lol_heatmap
