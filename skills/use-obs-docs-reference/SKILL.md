---
name: use-obs-docs-reference
description: Find and verify official OBS API guidance for Input Activity development or review. Use for OBS module, source, properties, lifecycle, graphics, callbacks, frontend, media, or utility API work.
---

# Use OBS Documentation Reference

1. Start with `docs/obs-reference.md` and select the task map row that matches the work. Read the
   linked official OBS documentation before changing or reviewing OBS integration.
2. Read the pinned version from `buildspec.json`. The site at <https://docs.obsproject.com> may
   document a newer API, so verify every changed symbol's availability, signature, ownership,
   callback behavior, and thread/context requirements in OBS Studio source or generated artifacts
   for that exact version.
3. Use the official page for design intent and the matching-version declarations as the
   compatibility authority. Do not infer compatibility from the live-site version alone.
4. Preserve Input Activity invariants: keep OBS source IDs stable, preserve the actionable macOS
   Accessibility warning, and never log captured keystroke contents.
5. For a non-trivial OBS API change, record the official documentation page and matching-version
   source evidence in the pull request summary or validation notes. Run the repository's applicable
   formatting, build, and affected OBS manual checks.
