#pragma once

#include <obs.h>

#include <string>
#include <vector>

namespace sources {

class lol_dashboard_camera_visibility {
public:
	~lol_dashboard_camera_visibility();
	void sync(obs_source_t *dashboard, const std::string &camera_source_uuid);
	void fit_to_panel(int left, int top, int width, int height);
	void fit_item(obs_sceneitem_t *item, int left, int top, int width, int height);

private:
	struct item_state {
		obs_sceneitem_t *item{};
		obs_transform_info transform{};
		obs_sceneitem_crop crop{};
		bool visible{};
	};
	std::vector<item_state> fitted_items_;
	obs_source_t *dashboard_{};
	obs_source_t *camera_{};
	void restore();
};

} // namespace sources
