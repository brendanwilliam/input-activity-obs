#include "lol_game_report_source.hpp"

#include "lol_game_report_collector.hpp"
#include "lol_game_report_store.hpp"

#include <QBuffer>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QStandardPaths>
#include <optional>
#include <obs-module.h>

extern "C" {
#include <graphics/graphics.h>
}

namespace sources {
namespace {
constexpr const char *source_id = "input-activity-lol-game-report";
constexpr const char *latest_key = "lol_game_report.show_latest";
constexpr const char *selected_key = "lol_game_report.selected";
constexpr const char *directory_key = "lol_game_report.export_directory";

class game_report_source {
public:
	game_report_source(obs_source_t *, obs_data_t *settings) { update(settings); }
	~game_report_source()
	{
		if (texture_) {
			obs_enter_graphics();
			gs_texture_destroy(texture_);
			obs_leave_graphics();
		}
	}
	void update(obs_data_t *settings)
	{
		show_latest_ = obs_data_get_bool(settings, latest_key);
		selected_ = QString::fromUtf8(obs_data_get_string(settings, selected_key));
		export_directory_ = QString::fromUtf8(obs_data_get_string(settings, directory_key));
	}
	void draw(gs_effect_t *effect)
	{
		QImage image = card(selected_report(), collector_.state());
		if (!texture_)
			texture_ = gs_texture_create(1920, 1080, GS_RGBA, 1, nullptr, GS_DYNAMIC);
		if (!texture_)
			return;
		gs_texture_set_image(texture_, image.constBits(), uint32_t(image.bytesPerLine()), false);
		gs_blend_state_push();
		gs_enable_blending(true);
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), texture_);
		gs_draw_sprite(texture_, 0, 1920, 1080);
		gs_blend_state_pop();
	}
	bool export_selected()
	{
		const auto report = selected_report();
		if (!report)
			return false;
		QImage image = card(report, lol_game_report::collection_state::empty);
		QByteArray png;
		QBuffer buffer(&png);
		buffer.open(QIODevice::WriteOnly);
		image.save(&buffer, "PNG");
		return store_.export_report(*report, export_directory_, png);
	}
	bool delete_selected()
	{
		const auto report = selected_report();
		if (!report)
			return false;
		return store_.remove(report->id);
	}
	QVector<lol_game_report::report> reports() const { return store_.reports(); }
	bool show_latest() const { return show_latest_; }
	QString collector_status() const { return lol_game_report::collector::state_text(collector_.state()); }

private:
	std::optional<lol_game_report::report> selected_report() const
	{
		const auto reports = store_.reports();
		if (reports.isEmpty())
			return std::nullopt;
		if (show_latest_)
			return reports.first();
		for (const auto &report : reports)
			if (report.id == selected_)
				return report;
		return std::nullopt;
	}
	static QImage card(const std::optional<lol_game_report::report> &report,
			   lol_game_report::collection_state state)
	{
		QImage image(1920, 1080, QImage::Format_RGBA8888_Premultiplied);
		image.fill(QColor("#10151f"));
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setPen(QColor("#e9eef8"));
		auto text = [&painter](const QRect &rect, int size, const QString &value,
				       QColor color = QColor("#e9eef8")) {
			painter.setPen(color);
			painter.setFont(QFont("Sans Serif", size, QFont::DemiBold));
			painter.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, value);
		};
		text({120, 92, 1600, 80}, 46, "League Game Report", QColor("#8fc8ff"));
		if (!report) {
			text({120, 260, 1600, 90}, 32, lol_game_report::collector::state_text(state));
			text({120, 355, 1600, 60}, 20,
			     "Reports are created after a completed game while this source exists.", QColor("#aab6c8"));
			return image;
		}
		text({120, 190, 1600, 60}, 28, lol_game_report::display_name(*report));
		text({120, 290, 500, 70}, 34, QString("%1  •  %2").arg(report->player, report->map));
		const auto last = report->samples.isEmpty() ? lol_game_report::stat_sample{} : report->samples.last();
		text({120, 385, 1500, 80}, 50,
		     QString("%1 / %2 / %3").arg(last.kills).arg(last.deaths).arg(last.assists), QColor("#ffffff"));
		text({120, 475, 1500, 45}, 24,
		     QString("CS %1   Level %2   Current gold %3   Ward score %4")
			     .arg(last.cs)
			     .arg(last.level)
			     .arg(last.gold)
			     .arg(last.ward_score),
		     QColor("#b9c7d9"));
		text({120, 580, 1600, 45}, 26, "Observed activity windows", QColor("#8fc8ff"));
		int y = 645;
		for (const auto &chapter : report->chapters.mid(0, 4)) {
			text({150, y, 1580, 60}, 20,
			     QString("%1:%2–%3:%4  %5")
				     .arg(chapter.start_seconds / 60)
				     .arg(chapter.start_seconds % 60, 2, 10, QLatin1Char('0'))
				     .arg(chapter.end_seconds / 60)
				     .arg(chapter.end_seconds % 60, 2, 10, QLatin1Char('0'))
				     .arg(chapter.summary),
			     QColor("#d0d9e6"));
			y += 72;
		}
		text({120, 990, 1600, 36}, 16,
		     "Self-only local data. Outcome unavailable when not provided by the local client.",
		     QColor("#8492a5"));
		return image;
	}
	lol_game_report::collector collector_;
	lol_game_report::store store_;
	bool show_latest_{true};
	QString selected_, export_directory_;
	gs_texture_t *texture_{};
};

bool export_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	return static_cast<game_report_source *>(data)->export_selected();
}
bool delete_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	return static_cast<game_report_source *>(data)->delete_selected();
}
bool latest_changed(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	obs_property_set_visible(obs_properties_get(props, selected_key), !obs_data_get_bool(settings, latest_key));
	return true;
}
obs_properties_t *properties(void *data)
{
	auto *props = obs_properties_create();
	auto *general = obs_properties_create();
	auto *latest = obs_properties_add_bool(general, latest_key, obs_module_text("LoLGameReport.ShowLatest"));
	auto *list = obs_properties_add_list(general, selected_key, obs_module_text("LoLGameReport.Selected"),
					     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	if (data)
		for (const auto &report : static_cast<game_report_source *>(data)->reports())
			obs_property_list_add_string(list, lol_game_report::display_name(report).toUtf8().constData(),
						     report.id.toUtf8().constData());
	const QString status = QString("%1: %2").arg(
		obs_module_text("LoLGameReport.CollectorStatus"),
		data ? static_cast<game_report_source *>(data)->collector_status()
		     : lol_game_report::collector::state_text(lol_game_report::collection_state::empty));
	obs_properties_add_text(general, "lol_game_report.collector_status", status.toUtf8().constData(),
				OBS_TEXT_INFO);
	obs_properties_add_group(props, "lol_game_report.report", obs_module_text("LoLGameReport.Report"),
				 OBS_GROUP_NORMAL, general);
	auto *actions = obs_properties_create();
	obs_properties_add_path(actions, directory_key, obs_module_text("LoLGameReport.ExportDirectory"),
				OBS_PATH_DIRECTORY, nullptr, nullptr);
	obs_properties_add_button2(actions, "lol_game_report.export", obs_module_text("LoLGameReport.Export"),
				   export_clicked, data);
	obs_properties_add_button2(actions, "lol_game_report.delete", obs_module_text("LoLGameReport.Delete"),
				   delete_clicked, data);
	obs_properties_add_group(props, "lol_game_report.actions", obs_module_text("Preferences.Actions"),
				 OBS_GROUP_NORMAL, actions);
	obs_property_set_modified_callback(latest, latest_changed);
	obs_property_set_visible(list, !data || !static_cast<game_report_source *>(data)->show_latest());
	return props;
}
void defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, latest_key, true);
	obs_data_set_default_string(
		settings, directory_key,
		QStandardPaths::writableLocation(QStandardPaths::PicturesLocation).toUtf8().constData());
}
} // namespace
void register_lol_game_report_source()
{
	obs_source_info info{};
	info.id = source_id;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = [](void *) {
		return obs_module_text("LoLGameReport");
	};
	info.create = [](obs_data_t *settings, obs_source_t *source) {
		return static_cast<void *>(new game_report_source(source, settings));
	};
	info.destroy = [](void *data) {
		delete static_cast<game_report_source *>(data);
	};
	info.update = [](void *data, obs_data_t *settings) {
		static_cast<game_report_source *>(data)->update(settings);
	};
	info.video_render = [](void *data, gs_effect_t *effect) {
		static_cast<game_report_source *>(data)->draw(effect);
	};
	info.get_width = [](void *) {
		return 1920U;
	};
	info.get_height = [](void *) {
		return 1080U;
	};
	info.get_properties = properties;
	info.get_defaults = defaults;
	obs_register_source(&info);
}
} // namespace sources
