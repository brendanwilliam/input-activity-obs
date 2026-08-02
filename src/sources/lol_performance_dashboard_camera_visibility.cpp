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
	int content_left, content_top, content_width, content_height;
};

bool find_dashboard_item(obs_scene_t *, obs_sceneitem_t *item, void *data)
{
	auto *context = static_cast<fit_context *>(data);
	if (obs_sceneitem_get_source(item) == context->dashboard) {
		struct matrix4 transform;
		obs_sceneitem_get_draw_transform(item, &transform);
		auto map_rectangle = [&](int &left, int &top, int &width, int &height) {
			struct vec3 first{float(left), float(top), 0.0f};
			struct vec3 second{float(left + width), float(top + height), 0.0f};
			vec3_transform(&first, &first, &transform);
			vec3_transform(&second, &second, &transform);
			left = int(std::lround(std::min(first.x, second.x)));
			top = int(std::lround(std::min(first.y, second.y)));
			width = std::max(1, int(std::lround(std::abs(second.x - first.x))));
			height = std::max(1, int(std::lround(std::abs(second.y - first.y))));
		};
		map_rectangle(context->left, context->top, context->width, context->height);
		map_rectangle(context->content_left, context->content_top, context->content_width,
			      context->content_height);
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
	context->visibility->fit_item(item, context->left, context->top, context->width, context->height,
				      context->content_left, context->content_top, context->content_width,
				      context->content_height);
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

void lol_dashboard_camera_visibility::fit_to_panel(int left, int top, int width, int height, int content_left,
						   int content_top, int content_width, int content_height)
{
	if (!camera_ || width < 1 || height < 1)
		return;
	fit_context context{this,   dashboard_,   camera_,     left,          top,           width,
			    height, content_left, content_top, content_width, content_height};
	obs_enum_scenes(
		[](void *data, obs_source_t *scene_source) {
			auto *context = static_cast<fit_context *>(data);
			obs_scene_t *scene = obs_scene_from_source(scene_source);
			if (!scene)
				return true;
			fit_context mapped{context->visibility,    context->dashboard,     context->camera,
					   context->left,          context->top,           context->width,
					   context->height,        context->content_left,  context->content_top,
					   context->content_width, context->content_height};
			obs_scene_enum_items(scene, find_dashboard_item, &mapped);
			if (mapped.dashboard)
				return true;
			obs_scene_enum_items(scene, fit_camera_item, &mapped);
			return true;
		},
		&context);
}

void lol_dashboard_camera_visibility::fit_item(obs_sceneitem_t *item, int left, int top, int width, int height,
					       int content_left, int content_top, int content_width, int content_height)
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
	const uint32_t source_width = obs_source_get_width(camera_);
	const uint32_t source_height = obs_source_get_height(camera_);
	const auto map_coordinate = [](int coordinate, int origin, int extent, uint32_t source_extent) {
		return std::clamp(int(std::lround(double(coordinate - origin) * source_extent / extent)), 0,
				  int(source_extent));
	};
	const int crop_left = map_coordinate(left, content_left, content_width, source_width);
	const int crop_top = map_coordinate(top, content_top, content_height, source_height);
	const int crop_right = map_coordinate(left + width, content_left, content_width, source_width);
	const int crop_bottom = map_coordinate(top + height, content_top, content_height, source_height);
	const obs_sceneitem_crop crop{crop_left, crop_top, std::max(0, int(source_width) - crop_right),
				      std::max(0, int(source_height) - crop_bottom)};
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
