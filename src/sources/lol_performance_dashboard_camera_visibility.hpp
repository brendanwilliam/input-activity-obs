#pragma once

#include <string>
#include <vector>

struct obs_scene_item;
struct obs_source;
typedef struct obs_scene_item obs_sceneitem_t;
typedef struct obs_source obs_source_t;

namespace sources {

class lol_dashboard_camera_visibility {
public:
	~lol_dashboard_camera_visibility();
	void sync(obs_source_t *dashboard, const std::string &camera_source_uuid);
	void activate();
	void deactivate();
	void hide(obs_sceneitem_t *item);

private:
	std::vector<obs_sceneitem_t *> hidden_items_;
	obs_source_t *dashboard_{};
	obs_source_t *camera_{};
	bool active_{}, showing_{};
	void restore();
};

} // namespace sources
