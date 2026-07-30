---
name: check-code-size
description: Check Input Activity source-file size and ownership before or after a refactor. Use when adding substantial C++, reviewing source layout, or splitting a large implementation.
---

# Check code size

1. Run `skills/check-code-size/scripts/check-code-size.sh`.
2. Keep the activity-source facade and every module in `src/sources/activity/` at or below 400
   lines. Split files by stable responsibility, never by arbitrary line range.
3. Verify shared setting keys, migrations, and rendering helpers have one owner. Source-specific
   rendering and property code belong with that source type.
4. Format edited C++ with clang-format 19 and run the macOS CI-equivalent build.
