#include "league_safe_area_source.hpp"

#include "league_safe_area_layout.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <memory>
#include <mutex>
#include <obs-module.h>

extern "C" {
#include <graphics/graphics.h>
}

namespace sources {
namespace {
constexpr const char *source_id = "input-activity-league-safe-area";
constexpr const char *path_key = "league_safe_area.game_cfg";

class league_safe_area_source {
public:
	league_safe_area_source(obs_source_t *source, obs_data_t *settings) : source(source)
	{
		QObject::connect(&watcher, &QFileSystemWatcher::fileChanged,
				 [this](const QString &) { request_reload(); });
		QObject::connect(&watcher, &QFileSystemWatcher::directoryChanged,
				 [this](const QString &) { request_reload(); });
		update(settings);
	}
	~league_safe_area_source()
	{
		if (texture) {
			obs_enter_graphics();
			gs_texture_destroy(texture);
			obs_leave_graphics();
		}
	}

	void update(obs_data_t *settings)
	{
		const QString updated_path = QString::fromUtf8(obs_data_get_string(settings, path_key));
		if (updated_path == path)
			return;
		path = updated_path;
		watcher.removePaths(watcher.files());
		watcher.removePaths(watcher.directories());
		if (!path.isEmpty()) {
			watcher.addPath(QFileInfo(path).absolutePath());
			if (QFileInfo::exists(path))
				watcher.addPath(path);
		}
		last_stamp = {};
		reload_requested = true;
		debounce_seconds = 0.25F;
		set_status(path.isEmpty() ? "Choose a League game.cfg file" : "Loading game.cfg…");
	}

	void tick(float seconds)
	{
		poll_seconds += seconds;
		if (poll_seconds >= 0.5F) {
			poll_seconds = 0.0F;
			const QFileInfo info(path);
			const auto stamp = std::make_pair(info.lastModified().toMSecsSinceEpoch(), info.size());
			if (stamp != last_stamp) {
				last_stamp = stamp;
				reload_requested = true;
				debounce_seconds = 0.25F;
			}
		}
		if (reload_requested && (debounce_seconds -= seconds) <= 0.0F)
			reload();
	}

	void draw(gs_effect_t *effect)
	{
		std::shared_ptr<const league_safe_area::model> current;
		{
			std::lock_guard<std::mutex> lock(model_mutex);
			current = layout;
		}
		if (!current)
			return;
		QImage image(current->game.width, current->game.height, QImage::Format_RGBA8888_Premultiplied);
		image.fill(Qt::transparent);
		QPainter painter(&image);
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(0, 255, 0, 80));
		for (const auto &safe : current->safe_regions) {
			const int left = static_cast<int>(safe.left * current->game.width);
			const int top = static_cast<int>(safe.top * current->game.height);
			const int right = static_cast<int>(safe.right * current->game.width);
			const int bottom = static_cast<int>(safe.bottom * current->game.height);
			painter.drawRect(left, top, std::max(0, right - left), std::max(0, bottom - top));
		}
		if (!texture || texture_width != current->game.width || texture_height != current->game.height) {
			gs_texture_destroy(texture);
			texture = gs_texture_create(current->game.width, current->game.height, GS_RGBA, 1, nullptr,
						    GS_DYNAMIC);
			texture_width = current->game.width;
			texture_height = current->game.height;
		}
		if (!texture)
			return;
		gs_texture_set_image(texture, image.constBits(), static_cast<uint32_t>(image.bytesPerLine()), false);
		gs_blend_state_push();
		gs_enable_blending(true);
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), texture);
		gs_draw_sprite(texture, 0, texture_width, texture_height);
		gs_blend_state_pop();
	}

	uint32_t width() const { return layout ? static_cast<uint32_t>(layout->game.width) : 1; }
	uint32_t height() const { return layout ? static_cast<uint32_t>(layout->game.height) : 1; }
	void request_reload()
	{
		reload_requested = true;
		debounce_seconds = 0.25F;
	}
	void auto_detect()
	{
		for (const QString &candidate : game_config_candidates()) {
			std::ifstream file(candidate.toStdString());
			if (!file)
				continue;
			const std::string contents((std::istreambuf_iterator<char>(file)),
						   std::istreambuf_iterator<char>());
			if (!league_safe_area::parse_game_config(contents).value)
				continue;
			obs_data_t *settings = obs_source_get_settings(source);
			obs_data_set_string(settings, path_key, candidate.toUtf8().constData());
			obs_source_update(source, settings);
			obs_data_release(settings);
			set_status("Detected League game.cfg");
			return;
		}
		set_status("Could not find a valid League game.cfg; choose it manually");
	}
	QString current_status() const
	{
		std::lock_guard<std::mutex> lock(status_mutex);
		return status;
	}

	QFileSystemWatcher watcher;
	std::atomic_bool reload_requested{false};

private:
	static QStringList game_config_candidates()
	{
		QStringList candidates;
#ifdef __APPLE__
		candidates << "/Applications/League of Legends.app/Contents/LoL/Config/game.cfg"
			   << QDir::homePath() + "/Applications/League of Legends.app/Contents/LoL/Config/game.cfg";
#elif defined(_WIN32)
		const auto add_install = [&candidates](const QString &root) {
			if (!root.isEmpty())
				candidates << QDir::fromNativeSeparators(root) +
						      "/Riot Games/League of Legends/Config/game.cfg";
		};
		add_install(qEnvironmentVariable("SystemDrive", "C:"));
		add_install(qEnvironmentVariable("ProgramFiles"));
		add_install(qEnvironmentVariable("ProgramW6432"));
		add_install(qEnvironmentVariable("ProgramFiles(x86)"));
		for (const QString &metadata :
		     {qEnvironmentVariable("ProgramData") + "/Riot Games/RiotClientInstalls.json",
		      qEnvironmentVariable("LOCALAPPDATA") + "/Riot Games/RiotClientInstalls.json"}) {
			QFile file(QDir::fromNativeSeparators(metadata));
			if (!file.open(QIODevice::ReadOnly))
				continue;
			QStringList values;
			collect_json_strings(QJsonDocument::fromJson(file.readAll()).object(), values);
			for (const QString &value : values) {
				const int game = value.indexOf("League of Legends", 0, Qt::CaseInsensitive);
				if (game >= 0)
					candidates << QDir::fromNativeSeparators(value.left(game + 17)) +
							      "/Config/game.cfg";
			}
		}
#endif
		candidates.removeDuplicates();
		return candidates;
	}

#ifdef _WIN32
	static void collect_json_strings(const QJsonValue &value, QStringList &strings)
	{
		if (value.isString()) {
			strings << value.toString();
		} else if (value.isArray()) {
			for (const QJsonValue &item : value.toArray())
				collect_json_strings(item, strings);
		} else if (value.isObject()) {
			for (const QJsonValue &item : value.toObject())
				collect_json_strings(item, strings);
		}
	}
#endif

	void reload()
	{
		reload_requested = false;
		if (path.isEmpty())
			return;
		std::ifstream file(path.toStdString());
		if (!file) {
			set_status("Waiting for game.cfg (using the last valid layout)");
			return;
		}
		const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		const auto parsed = league_safe_area::parse_game_config(contents);
		if (!parsed.value) {
			set_status(QString::fromStdString(parsed.error) + " (using the last valid layout)");
			return;
		}
		const auto next =
			std::make_shared<league_safe_area::model>(league_safe_area::make_model(*parsed.value));
		{
			std::lock_guard<std::mutex> lock(model_mutex);
			layout = std::move(next);
		}
		if (QFileInfo::exists(path) && !watcher.files().contains(path))
			watcher.addPath(path);
		set_status("Loaded game.cfg");
	}

	void set_status(const QString &value)
	{
		std::lock_guard<std::mutex> lock(status_mutex);
		status = value;
	}

	QString path;
	obs_source_t *source{};
	std::pair<qint64, qint64> last_stamp{};
	float poll_seconds{};
	float debounce_seconds{};
	gs_texture_t *texture{};
	int texture_width{};
	int texture_height{};
	std::shared_ptr<const league_safe_area::model> layout;
	mutable std::mutex model_mutex;
	QString status{"Choose a League game.cfg file"};
	mutable std::mutex status_mutex;
};

bool reload_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	static_cast<league_safe_area_source *>(data)->request_reload();
	return true;
}

bool auto_detect_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	static_cast<league_safe_area_source *>(data)->auto_detect();
	return true;
}

obs_properties_t *properties(void *data)
{
	auto *props = obs_properties_create();
	obs_properties_add_path(props, path_key, obs_module_text("LeagueSafeArea.GameCfg"), OBS_PATH_FILE, "game.cfg",
				nullptr);
	obs_properties_add_button2(props, "league_safe_area.auto_detect", obs_module_text("LeagueSafeArea.AutoDetect"),
				   auto_detect_clicked, data);
	const QString status = data ? static_cast<league_safe_area_source *>(data)->current_status() : "";
	obs_properties_add_text(props, "league_safe_area.status", status.toUtf8().constData(), OBS_TEXT_INFO);
	obs_properties_add_button2(props, "league_safe_area.reload", obs_module_text("LeagueSafeArea.Reload"),
				   reload_clicked, data);
	return props;
}
} // namespace

void register_league_safe_area_source()
{
	obs_source_info info{};
	info.id = source_id;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = [](void *) {
		return obs_module_text("LeagueSafeArea");
	};
	info.create = [](obs_data_t *settings, obs_source_t *source) {
		return static_cast<void *>(new league_safe_area_source(source, settings));
	};
	info.destroy = [](void *data) {
		delete static_cast<league_safe_area_source *>(data);
	};
	info.update = [](void *data, obs_data_t *settings) {
		static_cast<league_safe_area_source *>(data)->update(settings);
	};
	info.video_tick = [](void *data, float seconds) {
		static_cast<league_safe_area_source *>(data)->tick(seconds);
	};
	info.video_render = [](void *data, gs_effect_t *effect) {
		static_cast<league_safe_area_source *>(data)->draw(effect);
	};
	info.get_width = [](void *data) {
		return static_cast<league_safe_area_source *>(data)->width();
	};
	info.get_height = [](void *data) {
		return static_cast<league_safe_area_source *>(data)->height();
	};
	info.get_properties = properties;
	obs_register_source(&info);
}
} // namespace sources
