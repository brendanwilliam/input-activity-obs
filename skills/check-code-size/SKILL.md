---
name: check-code-size
description: Check source-file size, ownership, and grouping before or after a refactor. Use when adding substantial C++, reviewing source layout, or splitting a large implementation.
---

# Check code size

1. Run `skills/check-code-size/scripts/check-code-size.sh`.
2. Keep every source implementation module at or below 400 lines. Split files by stable
   responsibility, never by arbitrary line range.
3. Group each source implementation under a feature or shared-responsibility directory in
   `src/sources/`; implementation files do not belong directly in `src/sources/`.
4. Verify shared setting keys, migrations, and rendering helpers have one owner. Source-specific
   rendering and property code belong with that source type.
4. Format edited C++ with clang-format 19 and run the macOS CI-equivalent build.
