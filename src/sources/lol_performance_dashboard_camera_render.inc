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

void dashboard_source::render_camera(const QRect &mask, const QRect &bounds) const
{
	obs_source_t *camera = obs_get_source_by_uuid(camera_source_uuid_.c_str());
	if (!camera || camera == source_ || obs_source_get_type(camera) != OBS_SOURCE_TYPE_INPUT) {
		if (camera)
			obs_source_release(camera);
		return;
	}
	const uint32_t camera_width = obs_source_get_width(camera);
	const uint32_t camera_height = obs_source_get_height(camera);
	if (camera_width && camera_height) {
		const gs_rect scissor{mask.left(), int(height()) - mask.bottom() - 1, mask.width(), mask.height()};
		gs_set_scissor_rect(&scissor);
		gs_matrix_push();
		gs_matrix_translate3f(float(bounds.x()), float(bounds.y()), 0.0f);
		gs_matrix_scale3f(float(bounds.width()) / camera_width, float(bounds.height()) / camera_height, 1.0f);
		obs_source_video_render(camera);
		gs_matrix_pop();
		gs_set_scissor_rect(nullptr);
	}
	obs_source_release(camera);
}
