# OBS documentation reference

Use this guide for native OBS plugin work in Input Activity. It is a navigation and
compatibility reference, not an offline copy of upstream documentation.

## Version and authority

| Item | Value |
| --- | --- |
| Supported OBS Studio API | 31.1.1, pinned in [`buildspec.json`](../buildspec.json) |
| Official documentation | <https://docs.obsproject.com> |
| Live documentation reviewed | 32.2.1 on 2026-07-30 |
| Review scope | The complete `docs.obsproject.com` documentation navigation |

The live site is newer than the OBS version used to build this plugin. Use it to understand the
design and find APIs, but treat the 31.1.1 OBS source used by the build as authoritative for symbol
availability, declarations, ownership rules, callbacks, and behavior. Do not introduce an API just
because it appears in the live documentation.

The OBS wiki and the official plugin template are useful supplemental resources linked from the
site home page, but they are outside this guide's review scope.

## Where to look for this plugin

| Task | Start here | Also verify |
| --- | --- | --- |
| Module declaration, load/unload, locale, module text, module logging | [Plugins](https://docs.obsproject.com/plugins), [Modules](https://docs.obsproject.com/reference-modules) | `obs-module.h` in OBS 31.1.1 |
| Register or change a source type; choose source flags and callbacks | [Plugins: Sources](https://docs.obsproject.com/plugins#sources), [Sources (`obs_source_t`)](https://docs.obsproject.com/reference-core-objects#sources-obs-source-t) | `obs-source.h`; preserve this repository's stable source IDs |
| Create, update, migrate, save, or release source settings | [Data Settings (`obs_data_t`)](https://docs.obsproject.com/reference-core-objects#data-settings-obs-data-t) | `obs-data.h`; release returned references |
| Build source properties, lists, buttons, or modified callbacks | [Properties (`obs_properties_t`)](https://docs.obsproject.com/reference-core-objects#properties-obs-properties-t) | `obs-properties.h`; test the OBS properties UI |
| Register, load, save, or unregister hotkeys | [Core API](https://docs.obsproject.com/reference-core) | `obs-hotkey.h` and the 31.1.1 declarations |
| Render a source, manage textures, effects, or graphics context | [Rendering Graphics](https://docs.obsproject.com/graphics), [Graphics API](https://docs.obsproject.com/reference-libobs-graphics) | `graphics/graphics.h`; follow graphics-thread/context requirements |
| Use signals, procedures, calldata, or callbacks | [Callback API](https://docs.obsproject.com/reference-libobs-callback) | Callback ownership and threading in the matching source |
| Use `blog`, memory, paths, threading, config, or profiling helpers | [Platform/Utility API](https://docs.obsproject.com/reference-libobs-util) | `util/` headers; never log captured keystroke contents |
| Change video/audio/media handling | [Backend Design](https://docs.obsproject.com/backend-design), [Media I/O API](https://docs.obsproject.com/reference-libobs-media-io) | Exact 31.1.1 media declarations and thread constraints |
| Integrate with the OBS application UI, docks, scenes, or frontend events | [Frontends](https://docs.obsproject.com/frontends), [Frontend API](https://docs.obsproject.com/reference-frontend-api) | `obs-frontend-api.h` and whether this plugin links that API |

## Complete official-reference inventory

The following is the full navigation inventory reviewed from `docs.obsproject.com`. Use the linked
parent page and its table of contents to reach individual symbols.

| Area | Sections |
| --- | --- |
| [Core Concepts](https://docs.obsproject.com/) | [Backend Design](https://docs.obsproject.com/backend-design); [Plugins](https://docs.obsproject.com/plugins); [Frontends](https://docs.obsproject.com/frontends); [Rendering Graphics](https://docs.obsproject.com/graphics); [Python/Lua Scripting](https://docs.obsproject.com/scripting) |
| [OBS Core](https://docs.obsproject.com/reference-core) | Initialization, Shutdown, and Information; Libobs Objects; Video, Audio, and Graphics; Primary signal/procedure handlers; Core OBS Signals; Displays; Views |
| [Modules](https://docs.obsproject.com/reference-modules) | `obs_module_t`; Module Macros; Module Exports; Module Externs; Frontend Module Functions |
| [Core API Objects](https://docs.obsproject.com/reference-core-objects) | Sources (`obs_source_t`); Scenes (`obs_scene_t`); Outputs (`obs_output_t`); Encoders (`obs_encoder_t`); Services (`obs_service_t`); Data Settings (`obs_data_t`); Properties (`obs_properties_t`) |
| [Platform/Utility](https://docs.obsproject.com/reference-libobs-util) | Logging; Memory Management; Config Files; Dynamic Arrays; Double-Ended Queue; Dynamic Strings and String Helpers; Platform Helpers; Profiler; Serializer; Array Output Serializer; File Input/Output Serializers; Buffered File Output Serializer; Source Profiler; Text Lookup Interface; Threading |
| [Callbacks](https://docs.obsproject.com/reference-libobs-callback) | Calldata; Signals; Procedure Handlers |
| [Graphics](https://docs.obsproject.com/reference-libobs-graphics) | Effects (Shaders); 2-, 3-, and 4-Component Vectors; Quaternion; Matrix; Extra Math Functions/Macros; Image File Helper; Axis Angle; Core Graphics API |
| [Media I/O](https://docs.obsproject.com/reference-libobs-media-io) | Video Handler; Audio Handler; Resampler |
| [OBS Studio Frontend API](https://docs.obsproject.com/reference-frontend-api) | Structures/Enumerations; Functions |

## Development checklist

1. Identify the relevant row above and read the linked official material.
2. Compare every API added or changed with OBS Studio 31.1.1 source/build artifacts.
3. Follow documented lifetime, release, thread/context, and callback rules.
4. Preserve stable source IDs and the privacy boundary: never log keystroke contents.
5. For non-trivial OBS API changes, cite the official page and matching-version source evidence in the pull request.

## Refresh policy

Use `refresh-obs-docs-reference` whenever `buildspec.json` changes its `obs-studio` version or a
maintainer requests a documentation audit. Update the table metadata, every navigation area, task
map, links, and version-drift warning. Keep summaries concise; link upstream rather than copying
its prose.
