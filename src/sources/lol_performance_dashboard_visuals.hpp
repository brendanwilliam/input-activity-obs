#pragma once

#include "../input/input_data.hpp"

#include <QColor>
#include <QPointF>
#include <QRect>
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

class QPainter;

namespace sources {

struct lol_dashboard_theme {
	QColor inactive;
	QColor active;
	QColor background;
};

struct lol_dashboard_heatmap {
	QString gradient{"spectrum"};
	QColor low{235, 99, 37};
	QColor middle{250, 204, 21};
	QColor high{239, 68, 68};
};

QColor lol_dashboard_heatmap_color(const lol_dashboard_heatmap &heatmap, const lol_dashboard_theme &theme, int band);

class lol_dashboard_visuals {
public:
	void configure(const lol_dashboard_theme &theme, const lol_dashboard_heatmap &heatmap,
		       int rolling_window_seconds, const QRect &game_frame, const QRect &heatmap_bounds);
	void consume(const std::vector<input_data::trace_event> &events,
		     const input_data::button_map<uint16_t> &keyboard, const input_data::button_map<uint16_t> &mouse);
	void reset();
	void draw(QPainter &painter, const QRect &header, const QRect &heatmap, const QRect &summary, const QRect &keys,
		  bool right_aligned) const;

private:
	struct hex_bin {
		QPointF center;
		uint64_t value{};
	};
	struct active_key {
		uint16_t code;
		QString label;
		uint64_t fade_until{};
		uint64_t count{};
	};
	void advance(uint64_t now);
	void resize_heatmap(const QRect &bounds);
	void on_event(const input_data::trace_event &event);
	void draw_heatmap(QPainter &painter, const QRect &bounds) const;
	void draw_summary(QPainter &painter, const QRect &bounds, bool right_aligned) const;
	void draw_keys(QPainter &painter, const QRect &bounds, bool right_aligned) const;
	void draw_intensity(QPainter &painter, const QRect &bounds) const;
	QString distance_label() const;
	QString key_label(uint16_t code) const;
	size_t nearest_hex(const QPointF &point) const;

	lol_dashboard_theme theme_{{98, 94, 66}, {221, 193, 131}, {0, 0, 0, 0}};
	lol_dashboard_heatmap heatmap_;
	QRect game_frame_{0, 0, 1920, 1080}, heatmap_bounds_;
	std::vector<hex_bin> hex_bins_;
	std::optional<QPointF> last_heat_point_;
	std::optional<QPoint> last_distance_;
	std::optional<input_data::trace_event> last_motion_;
	std::unordered_map<uint16_t, bool> held_;
	std::unordered_map<uint16_t, uint64_t> press_counts_;
	std::vector<active_key> active_keys_;
	std::deque<std::array<double, 2>> samples_;
	std::array<double, 2> current_{};
	uint64_t bucket_start_{}, total_clicks_{};
	double distance_{};
	int window_{60};
};

} // namespace sources
