# Repository Agent Guide

## Policy

Use `feature/<kebab-title>`, `fix/<kebab-title>`, or `chore/<kebab-title>` branches from `main`. Target `main` through a pull request for all changes. Never push directly to `main` or bypass required checks.

For a coordinated group of multiple tasks implemented on the same day, use a dated integration branch named
`chore/updates-YYYY-MM-DD`, created from current `main`. Commit the related work to that branch and open one
pull request to `main` after the group is ready. Use a normal task-specific branch for isolated work; do not
use an update branch to avoid required checks.

Use Conventional Commit subjects such as `feat:`, `fix:`, `chore:`, `docs:`, `refactor:`, `test:`, `build:`, and `ci:`. Give non-trivial changes an explanatory body. Treat the pull request as the durable, human-facing change record.

Keep global input capture privacy-safe: preserve the actionable Accessibility warning and never log keystroke contents. Keep OBS source IDs stable.

## Repository layout

- `src/plugin-main.cpp`: plugin entry point.
- `src/hook/` and `src/input/`: capture and shared input state.
- `src/sources/`: OBS source implementations.
- `data/locale/en-US.ini`: user-facing strings.
- `cmake/`: packaging and platform configuration; never edit `build_macos/`.
- `deps/libuiohook/`: bundled dependency; change only when intentionally updating it.

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

`buildspec.json` is the compatibility authority: this plugin currently builds against OBS Studio
31.1.1. The live documentation can describe a newer OBS release, so verify exact API availability,
signatures, ownership, and lifecycle constraints in the matching OBS Studio source or generated
build artifacts before relying on it. Refresh the local reference whenever the pinned OBS version
changes, or when explicitly asked to audit OBS documentation.

## Repository-owned skills

- [`skills/start-change`](skills/start-change/SKILL.md): start a governed branch and synchronize with `main`.
- [`skills/prepare-pr`](skills/prepare-pr/SKILL.md): validate, document, and hand off a pull request.
- [`skills/release-macos-plugin`](skills/release-macos-plugin/SKILL.md): prepare and validate a signed/notarized release.
- [`skills/refresh-obs-docs-reference`](skills/refresh-obs-docs-reference/SKILL.md): audit the full OBS documentation site and refresh the local navigation reference.
- [`skills/use-obs-docs-reference`](skills/use-obs-docs-reference/SKILL.md): locate and verify OBS API documentation during development or review.

Install these into a local Codex skills directory with `./scripts/install-repository-skills.sh`.

Repository administrators can apply or reconcile the protected-branch rules with `./scripts/configure-github-rulesets.sh` after authenticating the GitHub CLI with ruleset administration permission.
