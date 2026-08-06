#include "sources/game_report/collection/lol_hexbin.hpp"

#include <algorithm>

namespace sources::lol_game_report {
double canonical_height(const hex_grid &grid)
{
	return lol_heatmap::canonical_height(grid);
}

QPointF hex_center(const hex_grid &grid, int column, int row)
{
	return lol_heatmap::center(grid, column, row);
}

hexbin nearest_hex(const hex_grid &grid, const QPointF &point)
{
	const lol_heatmap::cell cell = lol_heatmap::nearest_cell(grid, point);
	return {cell.column, cell.row, 0};
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
