#include "lol_hexbin.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sources::lol_game_report {
namespace {
constexpr double root_three = 1.7320508075688772935;
} // namespace

double canonical_height(const hex_grid &grid)
{
	return 100.0 * std::max(0.01, grid.frame_aspect_ratio);
}

QPointF hex_center(const hex_grid &grid, int column, int row)
{
	const double radius = std::clamp(grid.radius_percent, 1, 20);
	return {root_three * radius * (column + (row & 1 ? 0.5 : 0.0)), radius * (1.0 + 1.5 * row)};
}

hexbin nearest_hex(const hex_grid &grid, const QPointF &point)
{
	const double radius = std::clamp(grid.radius_percent, 1, 20);
	const int approximate_row = int(std::floor((point.y() / radius - 1.0) / 1.5 + 0.5));
	hexbin result{};
	double best_distance = std::numeric_limits<double>::infinity();
	for (int row = approximate_row - 2; row <= approximate_row + 2; ++row) {
		const double offset = row & 1 ? 0.5 : 0.0;
		const int approximate_column = int(std::floor(point.x() / (root_three * radius) - offset + 0.5));
		for (int column = approximate_column - 2; column <= approximate_column + 2; ++column) {
			const QPointF center = hex_center(grid, column, row);
			const double distance = std::hypot(point.x() - center.x(), point.y() - center.y());
			if (distance < best_distance ||
			    (distance == best_distance &&
			     (row < result.row || (row == result.row && column < result.column)))) {
				result = {column, row, 0};
				best_distance = distance;
			}
		}
	}
	return result;
}

void add_hex_dwell(QVector<hexbin> &bins, const hexbin &bin, uint64_t dwell_ms)
{
	if (!dwell_ms)
		return;
	auto found = std::find_if(bins.begin(), bins.end(), [&bin](const auto &value) {
		return value.column == bin.column && value.row == bin.row;
	});
	if (found == bins.end())
		bins.append({bin.column, bin.row, dwell_ms});
	else
		found->dwell_ms += dwell_ms;
}
} // namespace sources::lol_game_report
