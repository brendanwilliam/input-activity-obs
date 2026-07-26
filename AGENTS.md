# Repository Agent Guide

## Policy

Use `feature/<kebab-title>`, `fix/<kebab-title>`, or `chore/<kebab-title>` branches. Target `develop` for ordinary work; only a release pull request may merge `develop` into `main`. Never push directly to either branch or bypass required checks.

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

## Repository-owned skills

- [`skills/start-change`](skills/start-change/SKILL.md): start a governed branch and synchronize with `develop`.
- [`skills/prepare-pr`](skills/prepare-pr/SKILL.md): validate, document, and hand off a pull request.
- [`skills/release-macos-plugin`](skills/release-macos-plugin/SKILL.md): prepare and validate a signed/notarized release.

Install these into a local Codex skills directory with `./scripts/install-repository-skills.sh`.

Repository administrators can apply or reconcile the protected-branch rules with `./scripts/configure-github-rulesets.sh` after authenticating the GitHub CLI with ruleset administration permission.
