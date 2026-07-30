# Repository Agent Guide

## Policy

Use `feature/<kebab-title>`, `fix/<kebab-title>`, or `chore/<kebab-title>` branches from `main`. Target `main` through a pull request for all changes. Never push directly to `main` or bypass required checks.

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

- [`skills/start-change`](skills/start-change/SKILL.md): start a governed branch and synchronize with `main`.
- [`skills/prepare-pr`](skills/prepare-pr/SKILL.md): validate, document, and hand off a pull request.
- [`skills/release-macos-plugin`](skills/release-macos-plugin/SKILL.md): prepare and validate a signed/notarized release.

Install these into a local Codex skills directory with `./scripts/install-repository-skills.sh`.

Repository administrators can apply or reconcile the protected-branch rules with `./scripts/configure-github-rulesets.sh` after authenticating the GitHub CLI with ruleset administration permission.
