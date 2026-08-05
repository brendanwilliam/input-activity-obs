#include "sources/dashboard/rendering/lol_visuals.hpp"
#include "sources/dashboard/rendering/lol_key_labels.hpp"

#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
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

void ensure_dashboard_fonts_registered()
{
	static const bool registered = [] {
		for (const char *resource : {"fonts/ScienceGothic.ttf", "fonts/Inter.ttf"}) {
			char *path = obs_module_file(resource);
			if (path) {
				QFontDatabase::addApplicationFont(QString::fromUtf8(path));
				bfree(path);
			}
		}
		return true;
	}();
	Q_UNUSED(registered);
}

QFont dashboard_font(const lol_dashboard_font_style &style, QFont::Weight weight = QFont::Normal)
{
	QFont font(style.family, style.size, weight);
	font.setVariableAxis(QFont::Tag("opsz"), style.optical_size);
	font.setVariableAxis(QFont::Tag("wght"), style.weight);
	if (style.family == "Science Gothic") {
		font.setVariableAxis(QFont::Tag("wdth"), style.width);
		font.setVariableAxis(QFont::Tag("slnt"), style.slant);
	}
	return font;
}
} // namespace

void lol_dashboard_visuals::configure(const lol_dashboard_theme &theme, const lol_dashboard_heatmap &heatmap,
				      const lol_dashboard_regions &regions, int rolling_window_seconds,
				      const QRect &game_frame, const QRect &heatmap_bounds,
				      const lol_dashboard_style &style)
{
	theme_ = theme;
	regions_ = regions;
	const bool hex_size_changed = heatmap_.radius != heatmap.radius;
	heatmap_ = heatmap;
	style_ = style;
	window_ = std::clamp(rolling_window_seconds, 1, 60);
	game_frame_ = game_frame;
	if (heatmap_bounds != heatmap_bounds_ || hex_size_changed)
		resize_heatmap(heatmap_bounds);
}
void lol_dashboard_visuals::resize_heatmap(const QRect &bounds)
{
	heatmap_bounds_ = bounds;
	hex_bins_.clear();
	last_heat_point_.reset();
	const qreal hex_radius = heatmap_.radius;
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
		session_samples_.push_back(current_);
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
		active_keys_.push_back(
			{event.code, lol_dashboard_key_label(event.code), 0, ++press_counts_[event.code]});
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
QString lol_dashboard_visuals::distance_label() const
{
	double value = distance_ / 2800.0 * 2.54;
	QString unit = "cm";
	int decimals = value < 10.0 ? 2 : 1;
	if (value >= 100000.0) {
		value /= 100000.0;
		unit = "km";
		decimals = 3;
	} else if (value >= 1000.0) {
		value /= 100.0;
		unit = "m";
		decimals = 2;
	}
	return QString("%1 %2").arg(value, 0, 'f', decimals).arg(unit);
}
void lol_dashboard_visuals::draw_heatmap(QPainter &painter, const QRect &bounds) const
{
	painter.setClipRect(bounds);
	const qreal hex_radius = heatmap_.radius;
	std::vector<uint64_t> values;
	for (const auto &bin : hex_bins_)
		if (bin.value)
			values.push_back(bin.value);
	std::sort(values.begin(), values.end());
	const uint64_t q1 = values.empty() ? 0 : values[(values.size() - 1) / 4];
	const uint64_t q2 = values.empty() ? 0 : values[(values.size() - 1) / 2];
	const uint64_t q3 = values.empty() ? 0 : values[(values.size() - 1) * 3 / 4];
	for (const auto &bin : hex_bins_) {
		const int band = bin.value <= q1 ? 0 : bin.value <= q2 ? 1 : bin.value <= q3 ? 2 : 3;
		QColor fill = bin.value ? lol_dashboard_heatmap_color(heatmap_, theme_, band) : QColor(Qt::black);
		fill.setAlpha(bin.value ? 150 : 38);
		painter.setBrush(fill);
		painter.setPen(Qt::NoPen);
		if (bin.value)
			painter.setPen(QPen(lol_dashboard_heatmap_color(heatmap_, theme_, band), 0.75));
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
	Q_UNUSED(right_aligned);
	painter.setPen(Qt::white);
	const QRect content = bounds.adjusted(style_.section_padding, style_.section_padding, -style_.section_padding,
					      -style_.section_padding);
	painter.setFont(dashboard_font(style_.number_labels, QFont::Bold));
	const int label_height = style_.number_labels.size + style_.within_element_gap;
	const int value_height = style_.numbers.size + style_.within_element_gap;
	int top = content.top() + style_.element_padding;
	const auto alignment = Qt::AlignLeft | Qt::AlignVCenter;
	lol_dashboard_draw_shadowed_text(painter,
					 QRect(content.left() + style_.element_padding, top,
					       content.width() - 2 * style_.element_padding, label_height),
					 alignment, obs_module_text("MouseActivity.Distance"));
	top += label_height;
	painter.setFont(dashboard_font(style_.numbers, QFont::Bold));
	painter.setPen(theme_.active);
	lol_dashboard_draw_shadowed_text(painter,
					 QRect(content.left() + style_.element_padding, top,
					       content.width() - 2 * style_.element_padding, value_height),
					 alignment, distance_label());
	top += value_height + style_.element_y_gap;
	painter.setPen(Qt::white);
	painter.setFont(dashboard_font(style_.number_labels, QFont::Bold));
	lol_dashboard_draw_shadowed_text(painter,
					 QRect(content.left() + style_.element_padding, top,
					       content.width() - 2 * style_.element_padding, label_height),
					 alignment, obs_module_text("MouseActivity.Clicks"));
	top += label_height;
	painter.setFont(dashboard_font(style_.numbers, QFont::Bold));
	painter.setPen(theme_.active);
	lol_dashboard_draw_shadowed_text(painter,
					 QRect(content.left() + style_.element_padding, top,
					       content.width() - 2 * style_.element_padding, value_height),
					 alignment, QString::number(total_clicks_));
}
#include "sources/dashboard/rendering/lol_keys.inc"
void lol_dashboard_visuals::draw_intensity(QPainter &painter, const QRect &bounds) const
{
	const QRect section = bounds.adjusted(style_.section_padding, style_.section_padding, -style_.section_padding,
					      -style_.section_padding);
	const int card_width = std::max(1, (section.width() - style_.element_x_gap) / 2);
	for (int metric = 0; metric < 2; ++metric) {
		const QRect card(section.left() + metric * (card_width + style_.element_x_gap), section.top(),
				 card_width, section.height());
		std::vector<double> values;
		for (const auto &sample : session_samples_)
			values.push_back(metric == 0 ? sample[0] / 2800.0 * 2.54 : sample[1] * 60.0);
		std::array<double, 2> total = current_;
		for (const auto &sample : samples_) {
			total[metric] += sample[metric];
		}
		const double current = metric == 0 ? total[0] / window_ / 2800.0 * 2.54 : total[1] * 60.0 / window_;
		values.push_back(current);
		std::sort(values.begin(), values.end());
		const double min = values.front(), max = values.back(), range = std::max(0.001, max - min);
		const auto x = [&](double value) {
			return card.left() + 8 + int((value - min) / range * std::max(1, card.width() - 16));
		};
		const double q1 = values[(values.size() - 1) / 4], median = values[(values.size() - 1) / 2],
			     q3 = values[(values.size() - 1) * 3 / 4];
		const int y = card.top() + std::max(14, card.height() / 4);
		painter.setPen(Qt::white);
		painter.drawLine(x(min), y, x(max), y);
		painter.setBrush(theme_.inactive);
		painter.drawRect(QRect(std::min(x(q1), x(q3)), y - 7, std::max(1, std::abs(x(q3) - x(q1))), 14));
		painter.drawLine(x(median), y - 8, x(median), y + 8);
		painter.setPen(QPen(theme_.active, 3));
		painter.drawLine(x(current), y - 13, x(current), y + 13);
		painter.setPen(Qt::white);
		painter.setFont(dashboard_font(style_.number_labels));
		lol_dashboard_draw_shadowed_text(painter,
						 QRect(card.left() + style_.element_padding, y + 14,
						       card.width() - 2 * style_.element_padding,
						       style_.number_labels.size),
						 Qt::AlignLeft, QString::number(min, 'f', min < 10 ? 1 : 0));
		lol_dashboard_draw_shadowed_text(painter,
						 QRect(card.left() + style_.element_padding, y + 14,
						       card.width() - 2 * style_.element_padding,
						       style_.number_labels.size),
						 Qt::AlignRight, QString::number(max, 'f', max < 10 ? 1 : 0));
		painter.setFont(dashboard_font(style_.labels, QFont::Bold));
		lol_dashboard_draw_shadowed_text(painter,
						 QRect(card.left(), y + 14 + style_.number_labels.size, card.width(),
						       style_.labels.size),
						 Qt::AlignHCenter,
						 obs_module_text(metric ? "LoLPerformanceDashboard.APM"
									: "LoLPerformanceDashboard.MouseVelocity"));
		painter.setFont(dashboard_font(style_.numbers, QFont::Bold));
		painter.setPen(theme_.active);
		lol_dashboard_draw_shadowed_text(painter,
						 QRect(card.left(),
						       y + 14 + style_.number_labels.size + style_.labels.size,
						       card.width(), style_.numbers.size),
						 Qt::AlignHCenter, QString::number(current, 'f', current < 10 ? 1 : 0));
	}
}

void lol_dashboard_visuals::draw(QPainter &painter, const QRect &header, const QRect &heatmap, const QRect &summary,
				 const QRect &keys, bool right_aligned) const
{
	ensure_dashboard_fonts_registered();
	painter.fillRect(QRect(0, 0, std::max({header.right(), heatmap.right(), summary.right(), keys.right()}) + 1,
			       std::max({header.bottom(), heatmap.bottom(), summary.bottom(), keys.bottom()}) + 1),
			 theme_.background);
	if (regions_.intensity)
		draw_intensity(painter, header);
	if (regions_.mouse_activity)
		draw_heatmap(painter, heatmap.adjusted(style_.section_padding, style_.section_padding,
						       -style_.section_padding, -style_.section_padding));
	painter.setClipping(false);
	if (regions_.mouse_activity)
		draw_summary(painter, summary, right_aligned);
	if (regions_.keys)
		draw_keys(painter, keys, right_aligned);
}

} // namespace sources
