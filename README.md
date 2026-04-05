# Bit View — ImHex Plugin

A plugin for [ImHex](https://github.com/WerWolv/ImHex) that visualizes binary data as a grid of colored cells — one cell per bit.

![Bit Viewer screenshot](https://github.com/user-attachments/assets/7c8827fc-3d2c-41ad-9526-afa7dfaa57d7)

## Features

- **Bit grid visualization** — each bit rendered as a colored rectangle
- **Virtual scrolling** — only visible bits are read and rendered, handles files of any size (tested with 20 GB+)
- **Keyboard navigation** — arrow keys to move the cursor through bits, with edge-scrolling
- **Selection** — shift+arrow keys or mouse drag to select ranges of bits; Escape to collapse or clear
- **Configurable display** — pixel size, bit colors, MSB/LSB bit order
- **Flexible row width** — fit to window width, or set manually with math expressions (`1024 * 8`)
- **Hex Editor sync** — click a bit to select the byte in the Hex Editor; selecting in the Hex Editor scrolls and highlights in the Bit Viewer

## Installation

### From GitHub Releases

1. Download `bit_view-linux-x86_64.hexplug` or `bit_view-windows-x86_64.hexplug` from [Releases](https://github.com/andreymlv/ImHex-Plugin-Bit-View/releases)
2. Copy the `.hexplug` file to your ImHex plugins directory:
   - **Linux:** `~/.local/share/imhex/plugins/`
   - **Windows:** `%LocalAppData%/imhex/plugins/`
   - **macOS:** `~/Library/Application Support/imhex/plugins/`
3. Restart ImHex
4. Open **View → Bit Viewer**

### Building from source

#### Prerequisites

All dependencies required to build ImHex itself. See [ImHex INSTALL.md](https://github.com/WerWolv/ImHex/blob/master/INSTALL.md) for your platform.

#### Build

```bash
git clone --recursive https://github.com/andreymlv/ImHex-Plugin-Bit-View.git
cd ImHex-Plugin-Bit-View
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target bit_view -j$(nproc)
```

The plugin is at `build/bit_view.hexplug`. Copy it to your ImHex plugins directory.

#### Updating ImHex version

The plugin is built against a specific ImHex version (pinned as a git submodule). To update:

```bash
cd ImHex
git fetch --tags
git checkout v1.XX.X   # desired version
cd ..
# Update IMHEX_VERSION in CMakeLists.txt to match
```

## Compatibility

Built for ImHex **v1.38.1**. Plugin ABI may change between ImHex versions — use the matching version.

## License

GPLv2 — see [LICENSE.md](LICENSE.md)
