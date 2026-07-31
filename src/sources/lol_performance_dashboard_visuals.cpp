#include "lol_performance_dashboard_visuals.hpp"

#include <QFont>
#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <obs-module.h>
#include <util/platform.h>

namespace sources {
namespace {
constexpr uint64_t second_ns = 1000000000ULL;
constexpr uint64_t max_heatmap_gap_ns = 250000000ULL;
constexpr qreal hex_radius = 10.0;

QColor heat_color(int band)
{
	static const QColor colors[] = {{59, 130, 246}, {6, 182, 212}, {250, 204, 21}, {239, 68, 68}};
	return colors[band];
}
} // namespace

void lol_dashboard_visuals::configure(const lol_dashboard_theme &theme, int rolling_window_seconds,
				      const QRect &game_frame, const QRect &heatmap_bounds)
{
	theme_ = theme;
	window_ = std::clamp(rolling_window_seconds, 1, 60);
	game_frame_ = game_frame;
	if (heatmap_bounds != heatmap_bounds_)
		resize_heatmap(heatmap_bounds);
}

void lol_dashboard_visuals::resize_heatmap(const QRect &bounds)
{
	heatmap_bounds_ = bounds;
	hex_bins_.clear();
	last_heat_point_.reset();
	const qreal hex_width = std::sqrt(3.0) * hex_radius;
	const int columns = std::max(1, int(std::ceil(bounds.width() / hex_width)) + 1);
	const int rows = std::max(1, int(std::ceil(bounds.height() / (1.5 * hex_radius))) + 1);
	for (int row = 0; row < rows; ++row) {
		const qreal offset = row % 2 ? hex_width / 2.0 : 0.0;
		for (int column = 0; column < columns; ++column)
			hex_bins_.push_back({{bounds.left() + hex_width / 2 + offset + column * hex_width,
					      bounds.top() + hex_radius + row * 1.5 * hex_radius}});
	}
}

void lol_dashboard_visuals::consume(const std::vector<input_data::trace_event> &events,
				    const input_data::button_map<uint16_t> &keyboard,
				    const input_data::button_map<uint16_t> &)
{
	for (const auto &event : events)
		on_event(event);
	const uint64_t now = os_gettime_ns();
	advance(now);
	for (auto it = active_keys_.begin(); it != active_keys_.end();) {
		const auto found = keyboard.find(it->code);
		if (found != keyboard.end() && found->second) {
			held_[it->code] = true;
			++it;
		} else if (it->fade_until && it->fade_until <= now) {
			held_[it->code] = false;
			it = active_keys_.erase(it);
		} else {
			++it;
		}
	}
}

void lol_dashboard_visuals::advance(uint64_t now)
{
	if (!bucket_start_)
		bucket_start_ = now;
	while (now - bucket_start_ >= second_ns) {
		samples_.push_back(current_);
		if (samples_.size() > size_t(window_))
			samples_.pop_front();
		current_.fill(0.0);
		bucket_start_ += second_ns;
	}
}

void lol_dashboard_visuals::on_event(const input_data::trace_event &event)
{
	advance(event.time_ns);
	if (event.type == EVENT_KEY_PRESSED && !held_[event.code]) {
		held_[event.code] = true;
		active_keys_.erase(std::remove_if(active_keys_.begin(), active_keys_.end(),
						  [&](const auto &key) { return key.code == event.code; }),
				   active_keys_.end());
		active_keys_.push_back({event.code, key_label(event.code), 0, ++press_counts_[event.code]});
		++current_[1];
	} else if (event.type == EVENT_KEY_RELEASED) {
		held_[event.code] = false;
		for (auto &key : active_keys_)
			if (key.code == event.code)
				key.fade_until = event.time_ns + 2000000000ULL;
	} else if (event.type == EVENT_MOUSE_PRESSED) {
		++current_[1];
		++total_clicks_;
	}
	if (event.type != EVENT_MOUSE_MOVED && event.type != EVENT_MOUSE_DRAGGED)
		return;
	if (last_motion_)
		current_[0] += std::hypot(event.x - last_motion_->x, event.y - last_motion_->y);
	if (!game_frame_.contains(QPoint(event.x, event.y))) {
		last_motion_ = event;
		last_heat_point_.reset();
		last_distance_.reset();
		return;
	}
	const QPoint relative(event.x - game_frame_.left(), event.y - game_frame_.top());
	if (last_distance_)
		distance_ += std::hypot(relative.x() - last_distance_->x(), relative.y() - last_distance_->y());
	last_distance_ = relative;
	const QPointF point(
		heatmap_bounds_.left() + relative.x() * heatmap_bounds_.width() / std::max(1, game_frame_.width()),
		heatmap_bounds_.top() + relative.y() * heatmap_bounds_.height() / std::max(1, game_frame_.height()));
	if (last_motion_ && last_heat_point_ && event.time_ns > last_motion_->time_ns && !hex_bins_.empty())
		hex_bins_[nearest_hex(*last_heat_point_)].value +=
			std::min(event.time_ns - last_motion_->time_ns, max_heatmap_gap_ns);
	last_motion_ = event;
	last_heat_point_ = point;
}

size_t lol_dashboard_visuals::nearest_hex(const QPointF &point) const
{
	size_t result{};
	qreal best = std::numeric_limits<qreal>::max();
	for (size_t index = 0; index < hex_bins_.size(); ++index) {
		const qreal dx = point.x() - hex_bins_[index].center.x(), dy = point.y() - hex_bins_[index].center.y();
		if (dx * dx + dy * dy < best) {
			best = dx * dx + dy * dy;
			result = index;
		}
	}
	return result;
}

QString lol_dashboard_visuals::key_label(uint16_t code) const
{
	if (code >= VC_A && code <= VC_Z)
		return QString(QChar('A' + code - VC_A));
	if (code >= VC_0 && code <= VC_9)
		return QString(QChar('0' + code - VC_0));
	if (code >= VC_F1 && code <= VC_F12)
		return QString("F%1").arg(code - VC_F1 + 1);
	switch (code) {
	case VC_SPACE:
		return "␣";
	case VC_SHIFT_L:
	case VC_SHIFT_R:
		return "⇧";
	case VC_CONTROL_L:
	case VC_CONTROL_R:
		return "⌃";
	case VC_ALT_L:
	case VC_ALT_R:
		return "⌥";
	case VC_TAB:
		return "⇥";
	case VC_ENTER:
		return "↵";
	case VC_ESCAPE:
		return "⎋";
	default:
		return "?";
	}
}

QString lol_dashboard_visuals::distance_label() const
{
	double value = distance_ / 2800.0 * 2.54;
	QString unit = "cm";
	if (value > 10000.0) {
		value /= 100.0;
		unit = "m";
	}
	return QString("%1 %2").arg(value, 0, 'f', value < 10.0 ? 2 : 1).arg(unit);
}

void lol_dashboard_visuals::draw_heatmap(QPainter &painter, const QRect &bounds) const
{
	painter.setClipRect(bounds);
	std::vector<uint64_t> values;
	for (const auto &bin : hex_bins_)
		if (bin.value)
			values.push_back(bin.value);
	if (values.empty())
		return;
	std::sort(values.begin(), values.end());
	const uint64_t q1 = values[(values.size() - 1) / 4], q2 = values[(values.size() - 1) / 2],
		       q3 = values[(values.size() - 1) * 3 / 4];
	for (const auto &bin : hex_bins_) {
		if (!bin.value)
			continue;
		const int band = bin.value <= q1 ? 0 : bin.value <= q2 ? 1 : bin.value <= q3 ? 2 : 3;
		QColor fill = heat_color(band);
		fill.setAlpha(150);
		painter.setBrush(fill);
		painter.setPen(QPen(heat_color(band), 0.75));
		QPolygonF hexagon;
		for (int corner = 0; corner < 6; ++corner) {
			const qreal angle = (30.0 + corner * 60.0) * M_PI / 180.0;
			hexagon << QPointF(bin.center.x() + hex_radius * std::cos(angle),
					   bin.center.y() + hex_radius * std::sin(angle));
		}
		painter.drawPolygon(hexagon);
	}
}

void lol_dashboard_visuals::draw_summary(QPainter &painter, const QRect &bounds, bool right_aligned) const
{
	painter.setPen(Qt::white);
	painter.setFont(QFont("Silom", std::max(11, bounds.width() / 12), QFont::Bold));
	const int half = bounds.height() / 2;
	const auto alignment = right_aligned ? Qt::AlignRight : Qt::AlignLeft;
	painter.drawText(QRect(bounds.left(), bounds.top(), bounds.width(), half / 2), alignment | Qt::AlignVCenter,
			 obs_module_text("MouseActivity.Distance"));
	painter.drawText(QRect(bounds.left(), bounds.top() + half / 2, bounds.width(), half / 2),
			 alignment | Qt::AlignVCenter, distance_label());
	painter.drawText(QRect(bounds.left(), bounds.top() + half, bounds.width(), half / 2),
			 alignment | Qt::AlignVCenter, obs_module_text("MouseActivity.Clicks"));
	painter.drawText(QRect(bounds.left(), bounds.top() + half + half / 2, bounds.width(),
			       bounds.height() - half - half / 2),
			 alignment | Qt::AlignVCenter, QString::number(total_clicks_));
}

void lol_dashboard_visuals::draw_keys(QPainter &painter, const QRect &bounds, bool right_aligned) const
{
	painter.setPen(Qt::white);
	painter.setFont(QFont("Silom", std::max(11, bounds.width() / 12), QFont::Bold));
	const int pad = 6, title = 18, row = std::max(32, bounds.height() / 5);
	painter.drawText(bounds.adjusted(pad, 0, -pad, 0),
			 (right_aligned ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignTop, obs_module_text("LiveKeys"));
	const int visible = std::min(4, int(active_keys_.size()));
	for (int index = 0; index < visible; ++index) {
		const auto &key = active_keys_[active_keys_.size() - visible + index];
		const int width = std::max(1, (bounds.width() - pad * 2 - (visible - 1) * 4) / 4);
		const int x = right_aligned
				      ? bounds.right() - pad - (visible - index) * width - (visible - index - 1) * 4 + 1
				      : bounds.left() + pad + index * (width + 4);
		QColor fill = held_.count(key.code) && held_.at(key.code) ? theme_.active : theme_.inactive;
		painter.setBrush(fill);
		painter.setPen(Qt::NoPen);
		painter.drawRoundedRect(QRect(x, bounds.top() + title, width, row - 12), 5, 5);
		painter.setPen(Qt::white);
		painter.drawText(QRect(x, bounds.top() + title, width, row - 12), Qt::AlignCenter, key.label);
	}
	std::vector<active_key> keys;
	for (const auto &[code, count] : press_counts_)
		keys.push_back({code, key_label(code), 0, count});
	std::sort(keys.begin(), keys.end(), [](const auto &a, const auto &b) { return a.count > b.count; });
	if (keys.size() > 8)
		keys.resize(8);
	std::reverse(keys.begin(), keys.end());
	const uint64_t max =
		keys.empty() ? 1 : std::max_element(keys.begin(), keys.end(), [](const auto &a, const auto &b) {
					   return a.count < b.count;
				   })->count;
	for (int index = 0; index < int(keys.size()); ++index) {
		const int y = bounds.bottom() - (index + 1) * std::max(16, (bounds.height() - row) / 8) + 1;
		const int bar = std::max(1, int((bounds.width() - pad * 2) * keys[index].count / max));
		const QRect bar_rect(right_aligned ? bounds.right() - pad - bar + 1 : bounds.left() + pad, y + 11, bar,
				     6);
		painter.setBrush(theme_.inactive);
		painter.setPen(Qt::NoPen);
		painter.drawRoundedRect(bar_rect, 3, 3);
		painter.setPen(Qt::white);
		painter.drawText(QRect(bounds.left() + pad, y, bounds.width() - pad * 2, 11),
				 (right_aligned ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
				 QString("%1  %2").arg(keys[index].label).arg(keys[index].count));
	}
}

void lol_dashboard_visuals::draw_intensity(QPainter &painter, const QRect &bounds) const
{
	for (int metric = 0; metric < 2; ++metric) {
		const QRect card(bounds.left() + metric * bounds.width() / 2, bounds.top(), bounds.width() / 2,
				 bounds.height());
		std::vector<double> values;
		for (const auto &sample : samples_)
			values.push_back(metric == 0 ? sample[0] / 2800.0 * 2.54 : sample[1] * 60.0);
		const double current = metric == 0 ? current_[0] / 2800.0 * 2.54 : current_[1] * 60.0;
		values.push_back(current);
		std::sort(values.begin(), values.end());
		const double min = values.front(), max = values.back(), range = std::max(0.001, max - min);
		const auto x = [&](double value) {
			return card.left() + 8 + int((value - min) / range * std::max(1, card.width() - 16));
		};
		const double q1 = values[(values.size() - 1) / 4], median = values[(values.size() - 1) / 2],
			     q3 = values[(values.size() - 1) * 3 / 4];
		painter.setPen(Qt::white);
		painter.setFont(QFont("Silom", std::max(10, card.height() / 6), QFont::Bold));
		painter.drawText(card.adjusted(6, 0, -6, 0), Qt::AlignTop | Qt::AlignHCenter,
				 obs_module_text(metric ? "LoLPerformanceDashboard.APM"
							: "LoLPerformanceDashboard.MouseVelocity"));
		const int y = card.center().y();
		painter.drawLine(x(min), y, x(max), y);
		painter.setBrush(theme_.inactive);
		painter.drawRect(QRect(std::min(x(q1), x(q3)), y - 7, std::max(1, std::abs(x(q3) - x(q1))), 14));
		painter.drawLine(x(median), y - 8, x(median), y + 8);
		painter.setPen(QPen(theme_.active, 3));
		painter.drawLine(x(current), y - 13, x(current), y + 13);
		painter.setPen(Qt::white);
		painter.setFont(QFont("Silom", std::max(9, card.height() / 8)));
		painter.drawText(card.adjusted(6, 0, -6, -2), Qt::AlignBottom | Qt::AlignLeft,
				 QString::number(min, 'f', min < 10 ? 1 : 0));
		painter.drawText(card.adjusted(6, 0, -6, -2), Qt::AlignBottom | Qt::AlignRight,
				 QString::number(max, 'f', max < 10 ? 1 : 0));
	}
}

void lol_dashboard_visuals::draw(QPainter &painter, const QRect &header, const QRect &heatmap, const QRect &summary,
				 const QRect &keys, bool right_aligned) const
{
	painter.fillRect(QRect(0, 0, std::max({header.right(), heatmap.right(), summary.right(), keys.right()}) + 1,
			       std::max({header.bottom(), heatmap.bottom(), summary.bottom(), keys.bottom()}) + 1),
			 theme_.background);
	draw_intensity(painter, header);
	draw_heatmap(painter, heatmap);
	painter.setClipping(false);
	draw_summary(painter, summary, right_aligned);
	draw_keys(painter, keys, right_aligned);
}

} // namespace sources
