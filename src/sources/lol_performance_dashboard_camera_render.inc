std::optional<double> dashboard_source::camera_aspect() const
{
	if (!show_camera_ || camera_source_uuid_.empty())
		return {};
	obs_source_t *camera = obs_get_source_by_uuid(camera_source_uuid_.c_str());
	if (!camera || camera == source_ || obs_source_get_type(camera) != OBS_SOURCE_TYPE_INPUT) {
		if (camera)
			obs_source_release(camera);
		return {};
	}
	const uint32_t camera_width = obs_source_get_width(camera);
	const uint32_t camera_height = obs_source_get_height(camera);
	obs_source_release(camera);
	if (!camera_width || !camera_height)
		return {};
	return double(camera_width) / camera_height;
}

lol_dashboard_panels dashboard_source::panel_rectangles() const
{
	const auto aspect = camera_aspect();
	return lol_dashboard_panel_rectangles(
		*layout_,
		{aspect.has_value(), aspect.value_or(1.0), camera_width_percent_, camera_height_percent_,
		 camera_scale_percent_, camera_translate_x_percent_, camera_translate_y_percent_},
		{minimap_cover_.isNull() ? 1.0 : double(minimap_cover_.width()) / minimap_cover_.height(),
		 minimap_cover_width_percent_, minimap_cover_height_percent_, minimap_cover_scale_percent_,
		 minimap_cover_translate_x_percent_, minimap_cover_translate_y_percent_,
		 use_custom_minimap_cover_ ? minimap_cover_alpha_padding_percent_ : 0, use_custom_minimap_cover_});
}

void dashboard_source::render_camera(gs_effect_t *effect, const QRect &mask, const QRect &bounds)
{
	obs_source_t *camera = obs_get_source_by_uuid(camera_source_uuid_.c_str());
	if (!camera || camera == source_ || obs_source_get_type(camera) != OBS_SOURCE_TYPE_INPUT) {
		if (camera)
			obs_source_release(camera);
		return;
	}
	const uint32_t camera_width = obs_source_get_width(camera);
	const uint32_t camera_height = obs_source_get_height(camera);
	if (camera_width && camera_height && !mask.isEmpty() && !bounds.isEmpty()) {
		if (!camera_texture_)
			camera_texture_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		if (!camera_texture_) {
			obs_source_release(camera);
			return;
		}
		gs_texrender_reset(camera_texture_);
		if (gs_texrender_begin(camera_texture_, camera_width, camera_height)) {
			struct vec4 clear_color {};
			gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
			gs_ortho(0.0f, float(camera_width), 0.0f, float(camera_height), -100.0f, 100.0f);
			obs_source_video_render(camera);
			gs_texrender_end(camera_texture_);
		}
		gs_texture_t *texture = gs_texrender_get_texture(camera_texture_);
		const QRect visible = mask.intersected(bounds);
		if (texture && !visible.isEmpty()) {
			const auto source_coordinate = [](int position, int start, int extent, uint32_t source_extent) {
				return std::clamp(int(std::lround(double(position - start) * source_extent / extent)), 0,
						  int(source_extent));
			};
			const int source_left = source_coordinate(visible.left(), bounds.left(), bounds.width(), camera_width);
			const int source_top = source_coordinate(visible.top(), bounds.top(), bounds.height(), camera_height);
			const int source_right =
				source_coordinate(visible.right() + 1, bounds.left(), bounds.width(), camera_width);
			const int source_bottom =
				source_coordinate(visible.bottom() + 1, bounds.top(), bounds.height(), camera_height);
			const int source_width = std::max(1, source_right - source_left);
			const int source_height = std::max(1, source_bottom - source_top);
			gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), texture);
			gs_matrix_push();
			gs_matrix_translate3f(float(visible.x()), float(visible.y()), 0.0f);
			gs_matrix_scale3f(float(visible.width()) / source_width, float(visible.height()) / source_height, 1.0f);
			gs_draw_sprite_subregion(texture, 0, source_left, source_top, source_width, source_height);
			gs_matrix_pop();
		}
	}
	obs_source_release(camera);
}
