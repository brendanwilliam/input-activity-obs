# Repository Agent Guide

## Policy

Use `feature/<kebab-title>`, `fix/<kebab-title>`, or `chore/<kebab-title>` branches from current `develop`.
Target `develop` through a pull request for ordinary changes; it is the shared integration branch for parallel
work. Promote `develop` to `main` through a separate pull request only when the integrated set is ready.
Never push directly to either protected branch, and never bypass `main`'s required checks on the promotion PR.
Before starting feature work, fetch `origin` and create the task branch from the latest `origin/develop`.
Do not block ordinary feature work on `develop` containing `main`; reconcile main-only changes back into
`develop` through a dedicated pull request when that integration is needed.

For a coordinated group of closely related tasks, use a dated integration branch named
`chore/updates-YYYY-MM-DD`, created from current `develop`, and target its pull request at `develop`. Use a
normal task-specific branch for isolated work.

Use Conventional Commit subjects such as `feat:`, `fix:`, `chore:`, `docs:`, `refactor:`, `test:`, `build:`, and `ci:`. Give non-trivial changes an explanatory body. Treat the pull request as the durable, human-facing change record.

Keep global input capture privacy-safe: preserve the actionable Accessibility warning and never log keystroke contents. Keep OBS source IDs stable.

## Repository layout

- `src/plugin-main.cpp`: plugin entry point.
- `src/hook/` and `src/input/`: capture and shared input state.
- `src/sources/`: OBS source implementations.
- `data/locale/en-US.ini`: user-facing strings.
- `cmake/`: packaging and platform configuration; never edit `build_macos/`.
- `deps/libuiohook/`: bundled dependency; change only when intentionally updating it.

## Code organization

Keep new and materially refactored implementation modules under 400 lines. When a file approaches
that limit, split it by a stable responsibility (shared state, one OBS source type, rendering, or
properties) rather than by arbitrary line ranges. Group every `src/sources/` implementation in a
feature or shared-responsibility subdirectory; do not add implementation files directly to
`src/sources/`. Put cross-mode setting keys, shared rendering helpers, and migrations in one owned
module; do not duplicate them across mode files. Use `skills/check-code-size` before handing off a
refactor or adding a substantial implementation file.

## Validation

Format edited C, C++, and Objective-C++ files with clang-format 19. Format CMake and YAML with gersemi. Run the CI-equivalent configuration on macOS:

```sh
cmake --preset macos-ci
cmake --build --preset macos-ci
```

Manually test affected OBS source types and Accessibility behavior when capture changes.

## OBS documentation

For changes involving OBS APIs, module behavior, source types, source settings UI, graphics,
lifecycle, callbacks, or frontend APIs, use [`docs/obs-reference.md`](docs/obs-reference.md)
before implementing or reviewing the change. It maps this plugin's work to the official OBS
documentation at <https://docs.obsproject.com>.

For source-properties UI changes, also follow [`docs/settings-ui-guide.md`](docs/settings-ui-guide.md).

`buildspec.json` is the compatibility authority: this plugin currently builds against OBS Studio
31.1.1. The live documentation can describe a newer OBS release, so verify exact API availability,
signatures, ownership, and lifecycle constraints in the matching OBS Studio source or generated
build artifacts before relying on it. Refresh the local reference whenever the pinned OBS version
changes, or when explicitly asked to audit OBS documentation.

## Local development plugin refresh

After every commit, rebuild and reinstall the macOS plugin from the **currently checked-out working
branch** before handing off work for local testing. Never check out, build, or install `main`, `release`,
or another branch as a substitute for the branch containing the change.

First verify the current branch with `git branch --show-current`. Use one matching CMake configuration
for both build and installation; the normal development configuration is `RelWithDebInfo`:

```sh
cmake --preset macos
cmake --build --preset macos --config RelWithDebInfo
cmake --install build_macos --config RelWithDebInfo
```

Do not install a stale `Release` bundle after building `RelWithDebInfo`. Ask the user to fully quit and
reopen OBS after installation; do not control OBS unless explicitly asked.

## Repository-owned skills

- [`skills/start-change`](skills/start-change/SKILL.md): start a governed branch and synchronize with `develop`.
- [`skills/prepare-pr`](skills/prepare-pr/SKILL.md): validate, document, and hand off a pull request.
- [`skills/release-macos-plugin`](skills/release-macos-plugin/SKILL.md): prepare and validate a signed/notarized release.
- [`skills/refresh-obs-docs-reference`](skills/refresh-obs-docs-reference/SKILL.md): audit the full OBS documentation site and refresh the local navigation reference.
- [`skills/use-obs-docs-reference`](skills/use-obs-docs-reference/SKILL.md): locate and verify OBS API documentation during development or review.
- [`skills/check-code-size`](skills/check-code-size/SKILL.md): enforce source-module size, ownership, and grouping rules.

Install these into a local Codex skills directory with `./scripts/install-repository-skills.sh`.

Repository administrators can apply or reconcile the protected-branch rules with `./scripts/configure-github-rulesets.sh` after authenticating the GitHub CLI with ruleset administration permission.
