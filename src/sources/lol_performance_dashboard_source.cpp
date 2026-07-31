#include "activity_sources.hpp"

#include "../input/input_broker.hpp"
#include "league_safe_area_layout.hpp"

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QDir>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <memory>
#include <obs-module.h>
#include <util/platform.h>

extern "C" {
#include <graphics/graphics.h>
}

namespace sources {
namespace {
constexpr const char *source_id = "input-activity-lol-performance-dashboard";
constexpr const char *path_key = "lol_dashboard.game_cfg";
constexpr uint64_t second_ns = 1000000000ULL;

QColor color(uint32_t value)
{
	return {int(value & 0xff), int((value >> 8) & 0xff), int((value >> 16) & 0xff), int((value >> 24) & 0xff)};
}

class dashboard_source {
public:
	dashboard_source(obs_source_t *source, obs_data_t *settings) : source(source) { update(settings); }
	~dashboard_source()
	{
		if (texture) {
			obs_enter_graphics();
			gs_texture_destroy(texture);
			obs_leave_graphics();
		}
	}

	void update(obs_data_t *settings)
	{
		path = QString::fromUtf8(obs_data_get_string(settings, path_key));
		const int left = int(obs_data_get_int(settings, "lol_dashboard.frame_left"));
		const int top = int(obs_data_get_int(settings, "lol_dashboard.frame_top"));
		const int right = int(obs_data_get_int(settings, "lol_dashboard.frame_right"));
		const int bottom = int(obs_data_get_int(settings, "lol_dashboard.frame_bottom"));
		frame = {left, top, std::max(1, right - left + 1), std::max(1, bottom - top + 1)};
		window = std::clamp(int(obs_data_get_int(settings, "lol_dashboard.window")), 1, 60);
		inactive = color(uint32_t(obs_data_get_int(settings, "activity.inactive_color")));
		active = color(uint32_t(obs_data_get_int(settings, "activity.active_color")));
		reload();
	}

	void tick(float)
	{
		if (!layout)
			return;
		std::vector<input_data::trace_event> events;
		input_data::button_map<uint16_t> keys, mouse;
		input_broker::consume(target(), cursor, discard_backlog, events, keys, mouse);
		for (const auto &event : events)
			on_event(event);
		const uint64_t now = os_gettime_ns();
		advance(now);
		while (!key_rows.empty() && now - key_rows.front().first > 2000000000ULL)
			key_rows.pop_front();
	}

	void draw(gs_effect_t *effect)
	{
		if (!layout)
			return;
		const int width = layout->game.width, height = layout->game.height;
		QImage image(width, height, QImage::Format_RGBA8888_Premultiplied);
		image.fill(Qt::transparent);
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing);
		const panels p = panel_rectangles(width, height);
		draw_intensity(painter, p.header);
		draw_heatmap(painter, p.heatmap);
		draw_keys(painter, p.keys);
		if (!texture || texture_width != width || texture_height != height) {
			gs_texture_destroy(texture);
			texture = gs_texture_create(width, height, GS_RGBA, 1, nullptr, GS_DYNAMIC);
			texture_width = width;
			texture_height = height;
		}
		if (!texture)
			return;
		gs_texture_set_image(texture, image.constBits(), uint32_t(image.bytesPerLine()), false);
		gs_blend_state_push();
		gs_enable_blending(true);
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), texture);
		gs_draw_sprite(texture, 0, width, height);
		gs_blend_state_pop();
	}

	uint32_t width() const { return layout ? uint32_t(layout->game.width) : 1; }
	uint32_t height() const { return layout ? uint32_t(layout->game.height) : 1; }
	void reload()
	{
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
			return;
		const auto parsed = league_safe_area::parse_game_config(file.readAll().toStdString());
		if (parsed.value)
			layout = league_safe_area::make_model(*parsed.value);
	}
	void auto_detect()
	{
		for (const QString &candidate : game_config_candidates()) {
			QFile file(candidate);
			if (!file.open(QIODevice::ReadOnly) ||
			    !league_safe_area::parse_game_config(file.readAll().toStdString()).value)
				continue;
			obs_data_t *settings = obs_source_get_settings(source);
			obs_data_set_string(settings, path_key, candidate.toUtf8().constData());
			obs_source_update(source, settings);
			obs_data_release(settings);
			return;
		}
	}

private:
	static QStringList game_config_candidates()
	{
		QStringList candidates;
#ifdef __APPLE__
		candidates << "/Applications/League of Legends.app/Contents/LoL/Config/game.cfg"
			   << QDir::homePath() + "/Applications/League of Legends.app/Contents/LoL/Config/game.cfg";
#elif defined(_WIN32)
		for (const QString &root :
		     {qEnvironmentVariable("SystemDrive", "C:"), qEnvironmentVariable("ProgramFiles"),
		      qEnvironmentVariable("ProgramW6432"), qEnvironmentVariable("ProgramFiles(x86)")})
			if (!root.isEmpty())
				candidates << QDir::fromNativeSeparators(root) +
						      "/Riot Games/League of Legends/Config/game.cfg";
#endif
		candidates.removeDuplicates();
		return candidates;
	}
	struct panels {
		QRect header, heatmap, keys;
	};
	input_broker::target target() const
	{
		input_broker::target result;
		result.rectangle_enabled = true;
		result.rectangle_left = frame.left();
		result.rectangle_top = frame.top();
		result.rectangle_right = frame.right();
		result.rectangle_bottom = frame.bottom();
		return result;
	}
	QRect scaled(const league_safe_area::rect &r, int width, int height) const
	{
		return {int(std::lround(r.left * width)), int(std::lround(r.top * height)),
			int(std::lround((r.right - r.left) * width)), int(std::lround((r.bottom - r.top) * height))};
	}
	panels panel_rectangles(int width, int height) const
	{
		const auto &player = layout->exclusions[0];
		const auto &minimap = layout->exclusions[1];
		const auto &top_left = layout->exclusions[2];
		const auto &top_right = layout->exclusions[3];
		const auto &team = layout->exclusions[4];
		const bool left = layout->game.flip_minimap;
		const double side_left = left ? 0.0 : minimap.left;
		const double side_right = left ? minimap.right : 1.0;
		const double top = left ? top_left.bottom : top_right.bottom;
		const double bottom = layout->game.team_frames_left == left ? team.top : minimap.top;
		const league_safe_area::rect key_rect{side_left, top, side_right, std::max(top, bottom)};
		const league_safe_area::rect heat_rect =
			left ? league_safe_area::rect{player.right, player.top, 1.0, 1.0}
			     : league_safe_area::rect{0.0, player.top, player.left, 1.0};
		const league_safe_area::rect header_rect{top_left.right, 0.0, top_right.left, top_right.bottom};
		return {scaled(header_rect, width, height), scaled(heat_rect, width, height),
			scaled(key_rect, width, height)};
	}
	void advance(uint64_t now)
	{
		if (!bucket_start)
			bucket_start = now;
		while (now - bucket_start >= second_ns) {
			samples.push_back({velocity, actions});
			if (samples.size() > size_t(window))
				samples.pop_front();
			velocity = actions = 0;
			bucket_start += second_ns;
		}
	}
	void on_event(const input_data::trace_event &event)
	{
		advance(event.time_ns);
		if (event.type == EVENT_KEY_PRESSED) {
			++actions;
			key_rows.emplace_back(event.time_ns, QString::number(event.code));
		} else if (event.type == EVENT_MOUSE_PRESSED) {
			++actions;
		} else if (event.type == EVENT_MOUSE_MOVED || event.type == EVENT_MOUSE_DRAGGED) {
			if (last_motion)
				velocity += std::hypot(event.x - last_motion->x, event.y - last_motion->y);
			last_motion = event;
			if (frame.contains(QPoint(event.x, event.y))) {
				const int x =
					std::clamp((event.x - frame.left()) * 16 / std::max(1, frame.width()), 0, 15);
				const int y =
					std::clamp((event.y - frame.top()) * 9 / std::max(1, frame.height()), 0, 8);
				++heatmap[size_t(y * 16 + x)];
			}
		}
	}
	void box(QPainter &painter, const QRect &rect) const
	{
		painter.setPen(QPen(inactive, 1));
		QColor fill = inactive;
		fill.setAlpha(55);
		painter.setBrush(fill);
		painter.drawRoundedRect(rect.adjusted(0, 0, -1, -1), 6, 6);
	}
	void draw_intensity(QPainter &painter, const QRect &rect) const
	{
		const int split = rect.width() / 2;
		const std::array<QString, 2> labels{obs_module_text("LoLPerformanceDashboard.MouseVelocity"),
						    obs_module_text("LoLPerformanceDashboard.APM")};
		const std::array<double, 2> values{rate(true), rate(false)};
		for (int i = 0; i < 2; ++i) {
			const QRect card(rect.left() + i * split, rect.top(), i == 0 ? split : rect.width() - split,
					 rect.height());
			box(painter, card);
			painter.setPen(Qt::white);
			painter.setFont(QFont("Silom", std::max(10, card.height() / 5), QFont::Bold));
			painter.drawText(card.adjusted(8, 2, -8, 0), Qt::AlignTop | Qt::AlignHCenter, labels[i]);
			painter.setPen(active);
			painter.setFont(QFont("Silom", std::max(12, card.height() / 2), QFont::Bold));
			painter.drawText(card.adjusted(8, 0, -8, -2), Qt::AlignBottom | Qt::AlignHCenter,
					 QString::number(values[i], 'f', values[i] < 10 ? 1 : 0));
		}
	}
	void draw_heatmap(QPainter &painter, const QRect &rect) const
	{
		box(painter, rect);
		const uint64_t maximum = *std::max_element(heatmap.begin(), heatmap.end());
		if (!maximum)
			return;
		for (int y = 0; y < 9; ++y)
			for (int x = 0; x < 16; ++x) {
				const double amount = double(heatmap[size_t(y * 16 + x)]) / maximum;
				QColor c = active;
				c.setAlpha(int(40 + amount * 215));
				painter.setBrush(c);
				painter.setPen(Qt::NoPen);
				painter.drawRect(rect.left() + x * rect.width() / 16,
						 rect.top() + y * rect.height() / 9, rect.width() / 16 + 1,
						 rect.height() / 9 + 1);
			}
	}
	void draw_keys(QPainter &painter, const QRect &rect) const
	{
		box(painter, rect);
		painter.setPen(Qt::white);
		painter.setFont(QFont("Silom", std::max(12, rect.width() / 5), QFont::Bold));
		painter.drawText(rect.adjusted(8, 4, -8, -4), Qt::AlignTop | Qt::AlignHCenter,
				 obs_module_text("LiveKeys"));
		int y = rect.top() + rect.height() / 5;
		for (auto it = key_rows.rbegin(); it != key_rows.rend() && y < rect.bottom() - 24; ++it, y += 30) {
			painter.setBrush(active);
			painter.setPen(Qt::NoPen);
			painter.drawRoundedRect(QRect(rect.left() + 10, y, rect.width() - 20, 24), 4, 4);
			painter.setPen(Qt::white);
			painter.drawText(QRect(rect.left() + 10, y, rect.width() - 20, 24), Qt::AlignCenter,
					 it->second);
		}
	}
	double rate(bool velocity_metric) const
	{
		double total = velocity_metric ? velocity : actions;
		for (const auto &sample : samples)
			total += velocity_metric ? sample.first : sample.second;
		return total * 60.0 / std::max(1, window);
	}
	obs_source_t *source{};
	QString path;
	QRect frame{0, 0, 1920, 1080};
	int window{60};
	QColor inactive, active;
	std::optional<league_safe_area::model> layout;
	uint64_t cursor{}, bucket_start{};
	bool discard_backlog{};
	std::optional<input_data::trace_event> last_motion;
	std::array<uint64_t, 144> heatmap{};
	std::deque<std::pair<uint64_t, QString>> key_rows;
	std::deque<std::pair<double, double>> samples;
	double velocity{}, actions{};
	gs_texture_t *texture{};
	int texture_width{}, texture_height{};
};

bool reload_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	static_cast<dashboard_source *>(data)->reload();
	return true;
}
bool auto_detect_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	static_cast<dashboard_source *>(data)->auto_detect();
	return true;
}
obs_properties_t *properties(void *data)
{
	auto *p = obs_properties_create();
	obs_properties_add_path(p, path_key, obs_module_text("LeagueSafeArea.GameCfg"), OBS_PATH_FILE, "game.cfg",
				nullptr);
	obs_properties_add_button2(p, "lol_dashboard.auto_detect", obs_module_text("LeagueSafeArea.AutoDetect"),
				   auto_detect_clicked, data);
	obs_properties_add_button2(p, "lol_dashboard.reload", obs_module_text("LeagueSafeArea.Reload"), reload_clicked,
				   data);
	auto *frame = obs_properties_add_group(p, "lol_dashboard.frame",
					       obs_module_text("LoLPerformanceDashboard.GameFrame"), OBS_GROUP_NORMAL,
					       obs_properties_create());
	auto *f = obs_property_group_content(frame);
	for (const auto &[key, label] : std::array<std::pair<const char *, const char *>, 4>{
		     {{"lol_dashboard.frame_left", "LoLPerformanceDashboard.Left"},
		      {"lol_dashboard.frame_top", "LoLPerformanceDashboard.Top"},
		      {"lol_dashboard.frame_right", "LoLPerformanceDashboard.Right"},
		      {"lol_dashboard.frame_bottom", "LoLPerformanceDashboard.Bottom"}}})
		obs_properties_add_int(f, key, obs_module_text(label), -32768, 32767, 1);
	obs_properties_add_int_slider(p, "lol_dashboard.window", obs_module_text("LoLPerformanceDashboard.Window"), 1,
				      60, 1);
	return p;
}
} // namespace

void register_lol_performance_dashboard_source()
{
	obs_source_info info{};
	info.id = source_id;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = [](void *) {
		return obs_module_text("LoLPerformanceDashboard");
	};
	info.create = [](obs_data_t *s, obs_source_t *o) {
		return static_cast<void *>(new dashboard_source(o, s));
	};
	info.destroy = [](void *d) {
		delete static_cast<dashboard_source *>(d);
	};
	info.update = [](void *d, obs_data_t *s) {
		static_cast<dashboard_source *>(d)->update(s);
	};
	info.video_tick = [](void *d, float s) {
		static_cast<dashboard_source *>(d)->tick(s);
	};
	info.video_render = [](void *d, gs_effect_t *e) {
		static_cast<dashboard_source *>(d)->draw(e);
	};
	info.get_width = [](void *d) {
		return static_cast<dashboard_source *>(d)->width();
	};
	info.get_height = [](void *d) {
		return static_cast<dashboard_source *>(d)->height();
	};
	info.get_properties = properties;
	info.get_defaults = [](obs_data_t *s) {
		obs_data_set_default_int(s, "lol_dashboard.frame_right", 1919);
		obs_data_set_default_int(s, "lol_dashboard.frame_bottom", 1079);
		obs_data_set_default_int(s, "lol_dashboard.window", 60);
		obs_data_set_default_int(s, "activity.inactive_color", 0xff425e62);
		obs_data_set_default_int(s, "activity.active_color", 0xff83c1dd);
	};
	obs_register_source(&info);
}
} // namespace sources
