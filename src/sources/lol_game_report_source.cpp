#include "lol_game_report_source.hpp"

#include "lol_game_report_collector.hpp"
#include "lol_game_report_riot_api.hpp"
#include "lol_game_report_store.hpp"

#include <QBuffer>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <algorithm>
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
constexpr const char *page_key = "lol_game_report.page";
constexpr const char *normalize_key = "lol_game_report.normalize_average";
constexpr const char *series_key = "lol_game_report.chart_series";
constexpr const char *events_key = "lol_game_report.event_categories";
constexpr const char *dpi_key = "lol_game_report.mouse_dpi";
constexpr const char *auto_open_key = "lol_game_report.auto_open";
constexpr const char *development_logs_key = "lol_game_report.development_logs";

class game_report_source {
public:
	game_report_source(obs_source_t *, obs_data_t *settings)
	{
		riot_.set_diagnostics([this](const QJsonObject &fields) { collector_.log_riot_diagnostic(fields); });
		update(settings);
	}
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
		page_ = QString::fromUtf8(obs_data_get_string(settings, page_key));
		normalize_average_ = obs_data_get_bool(settings, normalize_key);
		series_ = QString::fromUtf8(obs_data_get_string(settings, series_key));
		event_categories_ = QString::fromUtf8(obs_data_get_string(settings, events_key));
		dpi_ = int(obs_data_get_int(settings, dpi_key));
		collector_.set_dpi(dpi_);
		collector_.set_auto_open(obs_data_get_bool(settings, auto_open_key));
		collector_.set_development_logs(obs_data_get_bool(settings, development_logs_key));
	}
	void draw(gs_effect_t *effect)
	{
		QImage image = card(selected_report(), collector_.state(), page_, normalize_average_, series_,
				    event_categories_);
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
	void tick() { collector_.tick(dpi_); }
	bool export_selected()
	{
		const auto report = selected_report();
		if (!report)
			return false;
		QImage image = card(report, lol_game_report::collection_state::empty, page_, normalize_average_,
				    series_, event_categories_);
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
	bool enrich_selected()
	{
		const auto report = selected_report();
		if (!report) {
			riot_status_ = "Select a saved report before enriching.";
			return false;
		}
		riot_status_ = "Requesting Riot Match-v5 data…";
		riot_.enrich(*report, [this](lol_game_report::report value, const QString &status) {
			if (status.startsWith("Riot Match-v5 enrichment complete"))
				store_.save(std::move(value));
			riot_status_ = status;
		});
		return true;
	}
	QVector<lol_game_report::report> reports() const { return store_.reports(); }
	bool show_latest() const { return show_latest_; }
	QString collector_status() const { return lol_game_report::collector::state_text(collector_.state()); }
	QString recap_url() const { return collector_.recap_url(); }
	QString riot_status() const { return riot_status_; }
	QString development_log_path() const { return collector_.development_log_path(); }
	bool development_logs_enabled() const { return collector_.development_logs_enabled(); }
	bool reveal_development_log() const
	{
		const QString path = development_log_path();
		return !path.isEmpty() && QProcess::startDetached("open", {"-R", path});
	}

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
			   lol_game_report::collection_state state, const QString &page, bool average_normalization,
			   const QString &series, const QString &event_categories)
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
		text({120, 92, 1600, 80}, 46, "League Game Report — " + page, QColor("#8fc8ff"));
		if (!report) {
			text({120, 260, 1600, 90}, 32, lol_game_report::collector::state_text(state));
			text({120, 355, 1600, 60}, 20,
			     "Reports are created after a completed game while this source exists.", QColor("#aab6c8"));
			return image;
		}
		text({120, 190, 1600, 60}, 28, lol_game_report::display_name(*report));
		text({120, 290, 500, 70}, 34, QString("%1  •  %2").arg(report->player, report->map));
		const auto last = report->samples.isEmpty() ? lol_game_report::stat_sample{} : report->samples.last();
		if (page == "performance") {
			int actions{};
			double distance{}, max_velocity{};
			for (const auto &sample : report->input_samples) {
				actions += sample.actions;
				distance += sample.mouse_distance_pixels;
				max_velocity = std::max(max_velocity, sample.max_velocity_pixels_per_second);
			}
			text({120, 395, 1600, 75}, 42,
			     QString("%1 actions  •  %2 px estimated mouse distance").arg(actions).arg(int(distance)));
			text({120, 485, 1600, 50}, 25,
			     QString("DPI snapshot: %1   Max velocity: %2 px/s").arg(report->dpi).arg(int(max_velocity)),
			     QColor("#b9c7d9"));
			painter.setPen(Qt::NoPen);
			for (const auto &bin : report->heatmap) {
				const int alpha = std::min(220, 30 + bin.count * 20);
				painter.setBrush(QColor(80, 190, 255, alpha));
				painter.drawEllipse(180 + bin.x * 12, 600 + bin.y * 12, 18, 18);
			}
			text({120, 990, 1600, 36}, 16,
			     "Mouse metrics are estimates from display-coordinate movement. Data stays on this device.",
			     QColor("#8492a5"));
			return image;
		}
		if (page == "game") {
			QVector<double> values;
			for (const auto &sample : report->samples)
				values.append(series == "cs"     ? sample.cs
					      : series == "gold" ? sample.estimated_gold
								 : sample.level);
			const auto normalized = lol_game_report::normalized_series(values, average_normalization);
			const int point_count = int(normalized.size());
			const int divisor = std::max(1, point_count - 1);
			painter.setPen(QPen(QColor("#8fc8ff"), 5));
			for (int n = 1; n < normalized.size(); ++n)
				painter.drawLine(120 + (n - 1) * 1500 / divisor, 820 - int(normalized[n - 1] * 350),
						 120 + n * 1500 / divisor, 820 - int(normalized[n] * 350));
			text({120, 370, 1600, 50}, 28,
			     QString("%1 progression (%2 normalization)")
				     .arg(series, average_normalization ? "game average" : "min–max"));
			int y = 450;
			for (const auto &event : report->events)
				if (event_categories.contains(event.category)) {
					text({150, y, 1500, 32}, 18,
					     QString("%1:%2  %3")
						     .arg(event.seconds / 60)
						     .arg(event.seconds % 60, 2, 10, QLatin1Char('0'))
						     .arg(event.type),
					     QColor("#d0d9e6"));
					y += 38;
					if (y > 700)
						break;
				}
			text({120, 930, 1600, 42}, 20,
			     "Level progression; the local game API does not provide fractional XP history.",
			     QColor("#b9c7d9"));
			return image;
		}
		text({120, 385, 1500, 80}, 50,
		     QString("%1 / %2 / %3").arg(last.kills).arg(last.deaths).arg(last.assists), QColor("#ffffff"));
		text({120, 475, 1500, 45}, 24,
		     QString("CS %1   Level %2   Current gold %3   Ward score %4")
			     .arg(last.cs)
			     .arg(last.level)
			     .arg(last.gold)
			     .arg(last.ward_score),
		     QColor("#b9c7d9"));
		text({120, 580, 1600, 45}, 26, "Insights", QColor("#8fc8ff"));
		int y = 645;
		const auto insights = lol_game_report::make_insights(*report);
		for (const auto &insight : insights.mid(0, 4)) {
			text({150, y, 1580, 60}, 20, insight.title + ": " + insight.detail, QColor("#d0d9e6"));
			y += 72;
		}
		text({120, 990, 1600, 36}, 16,
		     "Self-only local data. Riot Games is not endorsing or sponsoring this source.", QColor("#8492a5"));
		return image;
	}
	lol_game_report::collector collector_;
	lol_game_report::store store_;
	lol_game_report::riot_api riot_;
	bool show_latest_{true};
	QString selected_, export_directory_, page_{"summary"}, series_{"gold"},
		event_categories_{"kill,objective,tower,level"};
	bool normalize_average_{};
	int dpi_{800};
	QString riot_status_{"Riot enrichment has not run."};
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
bool enrich_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	return static_cast<game_report_source *>(data)->enrich_selected();
}
bool reveal_development_log_clicked(obs_properties_t *, obs_property_t *, void *data)
{
	return data && static_cast<game_report_source *>(data)->reveal_development_log();
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
	auto *page = obs_properties_add_list(general, page_key, obs_module_text("LoLGameReport.Page"),
					     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(page, obs_module_text("LoLGameReport.Page.Summary"), "summary");
	obs_property_list_add_string(page, obs_module_text("LoLGameReport.Page.Performance"), "performance");
	obs_property_list_add_string(page, obs_module_text("LoLGameReport.Page.Game"), "game");
	obs_properties_add_bool(general, normalize_key, obs_module_text("LoLGameReport.NormalizeAverage"));
	auto *series = obs_properties_add_list(general, series_key, obs_module_text("LoLGameReport.ChartSeries"),
					       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(series, "Estimated gold", "gold");
	obs_property_list_add_string(series, "CS", "cs");
	obs_property_list_add_string(series, "Level progression", "level");
	obs_properties_add_text(general, events_key, obs_module_text("LoLGameReport.EventCategories"),
				OBS_TEXT_DEFAULT);
	obs_properties_add_int(general, dpi_key, obs_module_text("LoLGameReport.MouseDPI"), 100, 32000, 50);
	obs_properties_add_bool(general, auto_open_key, obs_module_text("LoLGameReport.AutoOpen"));
	obs_properties_add_bool(general, development_logs_key, obs_module_text("LoLGameReport.DevelopmentLogs"));
	obs_properties_add_text(general, "lol_game_report.development_logs_warning",
				obs_module_text("LoLGameReport.DevelopmentLogsWarning"), OBS_TEXT_INFO);
	const QString development_log_path = data ? static_cast<game_report_source *>(data)->development_log_path()
						  : QString();
	obs_properties_add_text(general, "lol_game_report.development_logs_status",
				QString("%1: %2")
					.arg(obs_module_text("LoLGameReport.DevelopmentLogsStatus"),
					     development_log_path.isEmpty()
						     ? obs_module_text("LoLGameReport.DevelopmentLogsNone")
						     : development_log_path)
					.toUtf8()
					.constData(),
				OBS_TEXT_INFO);
	const QString local_url = data ? static_cast<game_report_source *>(data)->recap_url() : QString();
	obs_properties_add_text(
		general, "lol_game_report.local_url",
		QString("%1: %2").arg(obs_module_text("LoLGameReport.LocalURL"), local_url).toUtf8().constData(),
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
	obs_properties_add_button2(actions, "lol_game_report.enrich", obs_module_text("LoLGameReport.Enrich"),
				   enrich_clicked, data);
	auto *reveal = obs_properties_add_button2(actions, "lol_game_report.reveal_development_log",
						  obs_module_text("LoLGameReport.RevealDevelopmentLog"),
						  reveal_development_log_clicked, data);
	obs_property_set_enabled(reveal, data && !development_log_path.isEmpty());
	obs_properties_add_text(actions, "lol_game_report.riot_status",
				QString("%1: %2")
					.arg(obs_module_text("LoLGameReport.RiotStatus"),
					     data ? static_cast<game_report_source *>(data)->riot_status() : "")
					.toUtf8()
					.constData(),
				OBS_TEXT_INFO);
	obs_properties_add_group(props, "lol_game_report.actions", obs_module_text("Preferences.Actions"),
				 OBS_GROUP_NORMAL, actions);
	obs_property_set_modified_callback(latest, latest_changed);
	obs_property_set_visible(list, !data || !static_cast<game_report_source *>(data)->show_latest());
	return props;
}
void defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, latest_key, true);
	obs_data_set_default_string(settings, page_key, "summary");
	obs_data_set_default_string(settings, series_key, "gold");
	obs_data_set_default_string(settings, events_key, "kill,objective,tower,level");
	obs_data_set_default_int(settings, dpi_key, 800);
	obs_data_set_default_bool(settings, auto_open_key, true);
	obs_data_set_default_bool(settings, development_logs_key, false);
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
	info.video_tick = [](void *data, float) {
		static_cast<game_report_source *>(data)->tick();
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
