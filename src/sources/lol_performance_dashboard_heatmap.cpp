#include "lol_performance_dashboard_visuals.hpp"

#include <QPainter>
#include <cmath>

namespace sources {

void lol_dashboard_draw_shadowed_text(QPainter &painter, const QRect &bounds, Qt::Alignment alignment,
				      const QString &text)
{
	const QPen pen = painter.pen();
	painter.setPen(Qt::black);
	painter.drawText(bounds.translated(2, 2), alignment, text);
	painter.setPen(pen);
	painter.drawText(bounds, alignment, text);
}

void lol_dashboard_visuals::reset()
{
	for (auto &bin : hex_bins_)
		bin.value = 0;
	last_heat_point_.reset();
	last_distance_.reset();
	last_motion_.reset();
	held_.clear();
	press_counts_.clear();
	active_keys_.clear();
	samples_.clear();
	current_.fill(0.0);
	bucket_start_ = total_clicks_ = 0;
	distance_ = 0.0;
}

QColor lol_dashboard_heatmap_color(const lol_dashboard_heatmap &heatmap, const lol_dashboard_theme &theme, int band)
{
	if (heatmap.gradient == "theme")
		return band < 2 ? theme.inactive : theme.active;
	if (heatmap.gradient == "custom") {
		const qreal amount = band / 3.0;
		const QColor &from = amount <= 0.5 ? heatmap.low : heatmap.middle;
		const QColor &to = amount <= 0.5 ? heatmap.middle : heatmap.high;
		const qreal segment = amount <= 0.5 ? amount * 2.0 : (amount - 0.5) * 2.0;
		return {int(std::lround(from.red() + (to.red() - from.red()) * segment)),
			int(std::lround(from.green() + (to.green() - from.green()) * segment)),
			int(std::lround(from.blue() + (to.blue() - from.blue()) * segment))};
	}
	if (heatmap.gradient == "lime") {
		static const QColor colors[] = {{101, 163, 13}, {132, 204, 22}, {190, 242, 100}, {250, 204, 21}};
		return colors[band];
	}
	if (heatmap.gradient == "ocean") {
		static const QColor colors[] = {{30, 64, 175}, {14, 116, 144}, {34, 197, 94}, {250, 204, 21}};
		return colors[band];
	}
	static const QColor colors[] = {{59, 130, 246}, {6, 182, 212}, {250, 204, 21}, {239, 68, 68}};
	return colors[band];
}

} // namespace sources
