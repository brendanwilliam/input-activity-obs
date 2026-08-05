# Changelog

All notable changes to this project are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- A self-only local League Game Report OBS source with local report retention and PNG/JSON export.
- Opt-in, session-only development logs for League Game Report diagnostics.
- Local recap controls to save the rendered source JSON and load a Riot Match-v5 report by Game ID.
- LoL Performance Dashboard can link Game and Client macOS Screen Capture sources, automatically switching their scene-item visibility based on whether the League game is frontmost.
- LoL Performance Dashboard camera panels that render a selected OBS video-input source opposite the minimap, with safe-area-relative sizing, scale, and translation controls.
- LoL Performance Dashboard minimap covers with a packaged default image, custom image selection, scale control, and automatic flipped-minimap anchoring.
- A League Safe Area OBS source that reads `game.cfg` and renders placement-safe regions around persistent League HUD elements.
- LoL Performance Dashboard overlays for League, including placement-aware input visualizations, session intensity statistics, configurable hexbins, automatic game detection, and dashboard reset controls.
- A unified `Input Activity` OBS source with Live Keys, Mouse Activity, Input Intensity, and Input Statistics modes. Its shared appearance settings include a transparent-by-default background color.
- Separate Live Keys sizing for alphanumeric and special-key labels.
- A configurable Live Keys most-used bar chart alongside the live key row.
- Repository governance, release automation, and contributor guidance.
- Input targeting by display, application, or window.
- Shared Reset and Input Statistics Lap hotkeys, with optional lap metrics.
- Live Keys most-used mode, count and key font sizing, and configurable element spacing.
- Configurable metric rows and element spacing for Input Intensity.
- Configurable element spacing for Input Statistics.
- Mouse Activity heatmap controls for hexbin size and opacity, plus hotkey-driven PNG or SVG exports.
- Titled Input Intensity indicators with per-indicator keyboard scopes for all keys, letters, numbers, or a custom key list.
- Mouse Activity display toggles for the title, hexbin chart, live mouse trail, distance, and coordinates, with configurable pixel, metric, or imperial distance units.
- Mouse Activity custom tracking rectangles using inclusive desktop-coordinate corners.

### Changed

- League Game Report now persists a configurable game-frame-relative hexbin layout and mouse dwell time; older movement-count reports are clearly labelled as approximations.
- LoL Performance Dashboard now resets its statistics automatically when a new League game begins; the enabled-by-default setting can be turned off for uninterrupted manual sessions.
- League Game Report now scopes performance telemetry and heatmaps to the configured League game frame, reports press-based actions with real per-second velocity, and labels preliminary game-end gold and team data as pending until Riot enrichment completes.
- LoL Performance Dashboard and League Game Report now use Science Gothic for labels and controls, and Inter for live keys, metrics, timestamps, and chart data.
- The LoL Performance Dashboard now keeps its heatmap statistics top-aligned, restores the right-aligned key layout, limits dashboard visuals to the active game client, and renders linked cameras as cropped masks in either League mode. Minimap covers now use the same cropped-mask sizing and image-position controls.
- The LoL Performance Dashboard camera now stays visible outside League, and the new Always show dashboard option keeps all dashboard visuals visible outside the game.
- The LoL Performance Dashboard now reloads `game.cfg` changes while active so minimap covers immediately follow live minimap-size updates.
- The LoL Performance Dashboard now provides independent intensity, key, and mouse-activity visibility controls, and distance totals scale through centimetres, metres, and kilometres.
- Live Keys most-used bars now keep their configured vertical spacing and flow from the top of the chart when fewer than the configured number of keys are present.
- Input capture now queues raw events immediately and routes them on OBS video ticks, so adding source instances does not multiply capture work and the macOS input callback avoids application, window, display, and model processing.
- Live Keys now has clearer alignment and inversion behavior, consistent special-key indicators, and improved most-used key bars. Input Intensity box plots have more legible current-value markers, and Mouse Activity defaults to 720×560.
- Preference panels now use clear characteristic headings, retire unused defaults with legacy fallbacks, and hide Mouse Activity and Live Keys options until their parent feature is enabled.
- Rendering now falls back to Silom Regular when no explicit font is saved; target selectors adapt to the selected input target; Mouse Activity click labels use compact text-measured bounds; and Live Keys uses compact canonical key labels.
- New sources default to 20px padding, 10px element spacing, and a row-oriented Live Keys layout.
- Mouse Activity now places distance above the chart and coordinates below it, with shared left/right alignment and automatic metric or imperial distance scaling. Input Statistics no longer presents mouse distance totals.
- Legacy individual OBS source registrations have been removed; create and configure `Input Activity` for every mode.
- Build, formatting, and security automation now validate pull requests and pushes to `main`.

## [1.0.0] - 2026-07-26

### Added

- Initial Input Activity macOS OBS plugin release.
