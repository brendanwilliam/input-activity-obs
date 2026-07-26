# Local patches

This directory is a vendored `libuiohook` 1.3 snapshot. The upstream 1.3
branch was reviewed before the macOS compatibility fixes below were added.
It was not adopted directly because its post-event implementation removes APIs
and behavior required by this bundle, and it still uses the deprecated
`kIOMasterPortDefault` symbol.

Local macOS patches preserve the bundled API while building cleanly with the
macOS 12+ SDK and warnings treated as errors:

- use `kIOMainPortDefault` instead of the deprecated IOKit port constant;
- mark unused macOS callback and no-op text-delay parameters unused;
- return `UIOHOOK_SUCCESS` after a successful Unicode text post.

Repository formatters intentionally exclude this vendored dependency. Preserve
upstream formatting when updating it, and record any additional local patches
here.
