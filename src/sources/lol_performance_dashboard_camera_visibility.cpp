#include "lol_performance_dashboard_camera_visibility.hpp"

#include <obs-module.h>
#include <graphics/matrix4.h>
#include <graphics/vec3.h>

#include <algorithm>
#include <cmath>

namespace sources {
namespace {
struct fit_context {
	lol_dashboard_camera_visibility *visibility;
	obs_source_t *dashboard;
	obs_source_t *camera;
	int left, top, width, height;
};

bool find_dashboard_item(obs_scene_t *, obs_sceneitem_t *item, void *data)
{
	auto *context = static_cast<fit_context *>(data);
	if (obs_sceneitem_get_source(item) == context->dashboard) {
		struct matrix4 transform;
		obs_sceneitem_get_draw_transform(item, &transform);
		struct vec3 first{float(context->left), float(context->top), 0.0f};
		struct vec3 second{float(context->left + context->width), float(context->top + context->height), 0.0f};
		vec3_transform(&first, &first, &transform);
		vec3_transform(&second, &second, &transform);
		context->left = int(std::lround(std::min(first.x, second.x)));
		context->top = int(std::lround(std::min(first.y, second.y)));
		context->width = std::max(1, int(std::lround(std::abs(second.x - first.x))));
		context->height = std::max(1, int(std::lround(std::abs(second.y - first.y))));
		context->dashboard = nullptr;
		return false;
	}
	return true;
}

bool fit_camera_item(obs_scene_t *, obs_sceneitem_t *item, void *data)
{
	auto *context = static_cast<fit_context *>(data);
	if (obs_sceneitem_get_source(item) != context->camera)
		return true;
	context->visibility->fit_item(item, context->left, context->top, context->width, context->height);
	return true;
}
} // namespace

lol_dashboard_camera_visibility::~lol_dashboard_camera_visibility()
{
	restore();
}

void lol_dashboard_camera_visibility::restore()
{
	if (camera_)
		obs_source_release(camera_);
	camera_ = nullptr;
	for (const item_state &state : fitted_items_) {
		obs_sceneitem_set_info2(state.item, &state.transform);
		obs_sceneitem_set_crop(state.item, &state.crop);
		obs_sceneitem_set_visible(state.item, state.visible);
		obs_sceneitem_release(state.item);
	}
	fitted_items_.clear();
}

void lol_dashboard_camera_visibility::fit_to_panel(int left, int top, int width, int height)
{
	if (!camera_ || width < 1 || height < 1)
		return;
	fit_context context{this, dashboard_, camera_, left, top, width, height};
	obs_enum_scenes(
		[](void *data, obs_source_t *scene_source) {
			auto *context = static_cast<fit_context *>(data);
			obs_scene_t *scene = obs_scene_from_source(scene_source);
			if (!scene)
				return true;
			fit_context mapped{context->visibility, context->dashboard, context->camera, context->left,
					   context->top,        context->width,     context->height};
			obs_scene_enum_items(scene, find_dashboard_item, &mapped);
			if (mapped.dashboard)
				return true;
			obs_scene_enum_items(scene, fit_camera_item, &mapped);
			return true;
		},
		&context);
}

void lol_dashboard_camera_visibility::fit_item(obs_sceneitem_t *item, int left, int top, int width, int height)
{
	const auto saved = std::find_if(fitted_items_.begin(), fitted_items_.end(),
					[&](const item_state &state) { return state.item == item; });
	if (saved == fitted_items_.end()) {
		item_state state;
		state.item = item;
		obs_sceneitem_addref(item);
		obs_sceneitem_get_info2(item, &state.transform);
		obs_sceneitem_get_crop(item, &state.crop);
		state.visible = obs_sceneitem_visible(item);
		fitted_items_.push_back(state);
	}
	obs_transform_info transform{};
	transform.pos = {float(left), float(top)};
	transform.scale = {1.0f, 1.0f};
	transform.alignment = OBS_ALIGN_TOP | OBS_ALIGN_LEFT;
	transform.bounds_type = OBS_BOUNDS_SCALE_OUTER;
	transform.bounds_alignment = OBS_ALIGN_CENTER;
	transform.bounds = {float(width), float(height)};
	transform.crop_to_bounds = true;
	obs_sceneitem_set_info2(item, &transform);
	const obs_sceneitem_crop crop{};
	obs_sceneitem_set_crop(item, &crop);
	obs_sceneitem_set_visible(item, true);
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
	camera_ = camera;
}

} // namespace sources
