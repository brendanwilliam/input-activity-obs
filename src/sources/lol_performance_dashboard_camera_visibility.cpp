#include "lol_performance_dashboard_camera_visibility.hpp"

#include <obs-module.h>

namespace sources {
namespace {
struct visibility_context {
	lol_dashboard_camera_visibility *visibility;
	obs_source_t *camera;
};

bool hide_camera_scene_item(obs_scene_t *, obs_sceneitem_t *item, void *data)
{
	auto *context = static_cast<visibility_context *>(data);
	if (obs_sceneitem_get_source(item) == context->camera && obs_sceneitem_visible(item)) {
		obs_sceneitem_addref(item);
		context->visibility->hide(item);
		obs_sceneitem_set_visible(item, false);
	}
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, hide_camera_scene_item, data);
	return true;
}

bool hide_camera_scene(void *data, obs_source_t *scene_source)
{
	if (obs_scene_t *scene = obs_scene_from_source(scene_source))
		obs_scene_enum_items(scene, hide_camera_scene_item, data);
	return true;
}
} // namespace

lol_dashboard_camera_visibility::~lol_dashboard_camera_visibility()
{
	restore();
}

void lol_dashboard_camera_visibility::restore()
{
	if (active_camera_) {
		obs_source_remove_active_child(dashboard_, active_camera_);
		obs_source_release(active_camera_);
		active_camera_ = nullptr;
	}
	for (obs_sceneitem_t *item : hidden_items_) {
		obs_sceneitem_set_visible(item, true);
		obs_sceneitem_release(item);
	}
	hidden_items_.clear();
}

void lol_dashboard_camera_visibility::hide(obs_sceneitem_t *item)
{
	hidden_items_.push_back(item);
}

void lol_dashboard_camera_visibility::sync(obs_source_t *dashboard, const std::string &camera_source_uuid)
{
	restore();
	dashboard_ = dashboard;
	if (camera_source_uuid.empty())
		return;
	obs_source_t *camera = obs_get_source_by_uuid(camera_source_uuid.c_str());
	if (!camera || camera == dashboard || obs_source_get_type(camera) != OBS_SOURCE_TYPE_INPUT ||
	    !(obs_source_get_output_flags(camera) & OBS_SOURCE_VIDEO)) {
		if (camera)
			obs_source_release(camera);
		return;
	}
	visibility_context context{this, camera};
	obs_enum_scenes(hide_camera_scene, &context);
	if (obs_source_add_active_child(dashboard_, camera))
		active_camera_ = camera;
	else
		obs_source_release(camera);
}

} // namespace sources
