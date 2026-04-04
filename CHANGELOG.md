# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-04-04

### Added

- Keyboard navigation with arrow keys (up/down/left/right) to move cursor through bytes
- Multi-byte selection with Shift+arrow keys and mouse click-drag
- Bidirectional selection sync with the Hex Editor (click in either view to update the other)
- Escape key to deselect: first press collapses multi-byte selection to cursor, second press hides cursor
- Virtual scroll for large files — content sizes are capped at 10M pixels and scroll positions are mapped proportionally using double-precision arithmetic
- Compact read mode for very large bits-per-row values — only reads the visible column slice of each row instead of entire rows
- Hover tooltip shows byte address, bit position, and bit value
- Cursor and selection overlays with adaptive rendering: transparent fill at all sizes, plus crisp borders at pixel size 3+
- Math expression input for bits-per-row (e.g. `1024 * 8`) with evaluated result shown below

### Changed

- Default pixel size changed from 6 to 1
- Default bits-per-row changed to 8192 with fit-to-width off by default
- Bits-per-row eval result now shown on its own line below the input field instead of in a tooltip
- Selection overlay uses filled rectangles instead of outlines, visible at any pixel size

### Removed

- Cell gap setting (hardcoded to 0)

### Fixed

- Scroll jitter when holding arrow keys caused by ImGui's built-in keyboard navigation conflicting with plugin's own scroll handling (`ImGuiWindowFlags_NoNav`)
- Provider state mutations during rendering caused by synchronous `setSelection` event cascade — hex editor sync is now deferred to after `EndChild()`
- `handleScrollToSelection` using wrong visible row count from `GetContentRegionAvail()` inside child window (returns explicit content size, not visible area)
- Mouse drag selection losing anchor direction when dragging upward — sync echo no longer overwrites anchor/cursor

## [0.1.0] - 2025-04-15

### Added

- Initial Bit Viewer plugin for ImHex v1.38.1
- Visualizes binary data as a colored grid of bits (one cell per bit)
- Configurable pixel size, bit order (MSB/LSB first), and bit colors
- Fit-to-width mode and fixed bits-per-row with math expression support
- Click to select byte and sync with Hex Editor
- Hover tooltip with byte/bit information
- Russian and English localization
- GitHub Actions CI for Linux and Windows
