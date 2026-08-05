#include "sources/game_report/presentation/lol_source.hpp"

#include "sources/game_report/collection/lol_collector.hpp"
#include "sources/game_report/data/lol_store.hpp"
#include "sources/game_report/integration/lol_riot_api.hpp"
#include "sources/hud_layout/lol_layout.hpp"

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
constexpr const char *hex_radius_key = "lol_game_report.hex_radius_percent";
constexpr const char *auto_open_key = "lol_game_report.auto_open";
constexpr const char *development_logs_key = "lol_game_report.development_logs";
constexpr const char *game_cfg_key = "lol_game_report.game_cfg";
constexpr const char *frame_left_key = "lol_game_report.frame_left";
constexpr const char *frame_top_key = "lol_game_report.frame_top";

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
		hex_radius_percent_ = int(obs_data_get_int(settings, hex_radius_key));
		collector_.set_hex_radius_percent(hex_radius_percent_);
		game_cfg_path_ = QString::fromUtf8(obs_data_get_string(settings, game_cfg_key));
		frame_left_ = int(obs_data_get_int(settings, frame_left_key));
		frame_top_ = int(obs_data_get_int(settings, frame_top_key));
		collector_.set_game_frame(game_frame());
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
	void tick() { collector_.tick(dpi_, hex_radius_percent_); }
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
	QRect game_frame() const
	{
		QString path = game_cfg_path_;
#ifdef __APPLE__
		if (path.isEmpty()) {
			for (const QString &candidate :
			     {QString("/Applications/League of Legends.app/Contents/LoL/Config/game.cfg"),
			      QDir::homePath() + "/Applications/League of Legends.app/Contents/LoL/Config/game.cfg"}) {
				if (QFile::exists(candidate)) {
					path = candidate;
					break;
				}
			}
		}
#endif
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
			return {frame_left_, frame_top_, 1920, 1080};
		const auto parsed = league_safe_area::parse_game_config(file.readAll().toStdString());
		if (!parsed.value)
			return {frame_left_, frame_top_, 1920, 1080};
		const auto model = league_safe_area::make_model(*parsed.value);
		return {frame_left_, frame_top_, model.game.width, model.game.height};
	}
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
			QVector<uint64_t> dwell_values;
			for (const auto &bin : report->hexbins)
				dwell_values.append(bin.dwell_ms);
			std::sort(dwell_values.begin(), dwell_values.end());
			const QRectF frame(180, 580, 1260, 1260 / report->hex_geometry.frame_aspect_ratio);
			for (const auto &bin : report->hexbins) {
				const auto center =
					lol_game_report::hex_center(report->hex_geometry, bin.column, bin.row);
				const double scale = frame.width() / 100.0;
				const double radius = report->hex_geometry.radius_percent * scale;
				const int rank =
					int(std::upper_bound(dwell_values.cbegin(), dwell_values.cend(), bin.dwell_ms) -
					    dwell_values.cbegin()) -
					1;
				const int band =
					dwell_values.isEmpty() ? 0 : std::min(3, 4 * rank / int(dwell_values.size()));
				static const QColor colors[] = {QColor("#3b82f6"), QColor("#06b6d4"), QColor("#facc15"),
								QColor("#ef4444")};
				QPolygonF polygon;
				for (int n = 0; n != 6; ++n) {
					const double angle = M_PI / 6.0 + n * M_PI / 3.0;
					polygon << QPointF(frame.left() + center.x() * scale + radius * std::cos(angle),
							   frame.top() + center.y() * scale + radius * std::sin(angle));
				}
				painter.setBrush(colors[band]);
				painter.drawPolygon(polygon);
			}
			text({120, 920, 1600, 36}, 18,
			     report->hexbin_estimated ? "Legacy movement-density approximation"
						      : QString("Mouse dwell time · %1% frame-width hex radius")
								.arg(report->hex_geometry.radius_percent),
			     QColor("#b9c7d9"));
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
	int hex_radius_percent_{lol_game_report::default_hex_radius_percent};
	QString game_cfg_path_;
	int frame_left_{}, frame_top_{};
	QString riot_status_{"Riot enrichment has not run."};
	gs_texture_t *texture_{};
};

#include "sources/game_report/presentation/lol_properties.inc"

} // namespace sources
