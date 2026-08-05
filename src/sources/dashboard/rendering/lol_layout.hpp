#pragma once

#include "sources/hud_layout/lol_layout.hpp"

namespace sources {

struct lol_dashboard_rect {
	int x_{};
	int y_{};
	int width_{};
	int height_{};

	constexpr int x() const { return x_; }
	constexpr int y() const { return y_; }
	constexpr int width() const { return width_; }
	constexpr int height() const { return height_; }
	constexpr int left() const { return x_; }
	constexpr int top() const { return y_; }
	constexpr int right() const { return x_ + width_ - 1; }
	constexpr int bottom() const { return y_ + height_ - 1; }
	constexpr bool isEmpty() const { return width_ < 1 || height_ < 1; }
	void moveLeft(int value) { x_ = value; }
	void moveTop(int value) { y_ = value; }
	void translate(int horizontal, int vertical)
	{
		x_ += horizontal;
		y_ += vertical;
	}
	lol_dashboard_rect adjusted(int left_adjustment, int top_adjustment, int right_adjustment,
				    int bottom_adjustment) const
	{
		return {x_ + left_adjustment, y_ + top_adjustment, width_ + right_adjustment - left_adjustment,
			height_ + bottom_adjustment - top_adjustment};
	}
};

struct lol_dashboard_camera_layout {
	bool enabled{};
	double aspect{1.0};
	int width_percent{67};
	int height_percent{100};
	int scale_percent{100};
	int translate_x_percent{};
	int translate_y_percent{};
};

struct lol_dashboard_image_layout {
	double aspect{1.0};
	int width_percent{100};
	int height_percent{100};
	int scale_percent{100};
	int translate_x_percent{};
	int translate_y_percent{};
	int alpha_padding_percent{};
	bool fit_within_mask{};
};

struct lol_dashboard_panels {
	lol_dashboard_rect header, heatmap, summary, keys, camera_mask, camera, minimap_cover_mask, minimap_cover;
	bool right_aligned{};
	bool camera_visible{};
};

lol_dashboard_rect lol_dashboard_aspect_fit(const lol_dashboard_rect &bounds, double aspect);

lol_dashboard_panels lol_dashboard_panel_rectangles(const league_safe_area::model &layout,
						    const lol_dashboard_camera_layout &camera,
						    const lol_dashboard_image_layout &minimap_cover,
						    int hud_padding = 20);

} // namespace sources
