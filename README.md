# Jin Ng — VCV Rack modules

- **SEQ-101** (26HP): a four-track step sequencer whose steps store indices into per-track voltage tables. Inspired by the Orthogonal Devices ER-101.
- **SEQ-101ext** (14HP): an expander for it, adding parts, groups with an X/Y/Z modulation bus, recording, 127 snapshot slots and MIDI file import. Inspired by the Orthogonal Devices ER-102.

Independent re-creations of the hardware's published behaviour, not affiliated with or endorsed by Orthogonal Devices.

- **Installing** before the modules are in the VCV Library: [docs/INSTALL.md](docs/INSTALL.md) (downloads for Windows, macOS and Linux on the [Releases page](https://github.com/zoidsroy/JinNg-VCV/releases/latest))
- **User manual**: [docs/MANUAL.md](docs/MANUAL.md)
- Behaviour specifications, with every decision taken where the hardware manuals are silent (in Traditional Chinese): [docs/SPEC.md](docs/SPEC.md), [docs/SPEC-ER102.md](docs/SPEC-ER102.md)
- Changes: [CHANGELOG.md](CHANGELOG.md)

License: [GPL-3.0-or-later](LICENSE).

## Building

On Windows, with [MSYS2](https://www.msys2.org/) (MINGW64 shell):

```bash
pacman -Syu
# reopen the shell, then:
pacman -S --needed git wget make tar unzip zip mingw-w64-x86_64-gcc mingw-w64-x86_64-gdb mingw-w64-x86_64-cmake autoconf automake libtool jq python zstd mingw-w64-x86_64-pkgconf
```

Unpack the [Rack SDK](https://vcvrack.com/downloads/Rack-SDK-latest-win-x64.zip) next to this repository (as `../Rack-SDK`) or point `RACK_DIR` at it, then:

```bash
scripts/build.sh      # build, run both test suites, install into Rack's plugin folder
```

or step by step:

```bash
make                  # the plugin
make test             # unit tests for src/core (no Rack needed)
make test-rack        # serialization round trip (links libRack; set RACK_INSTALL to Rack's folder)
make install          # into %LOCALAPPDATA%\Rack2\plugins-win-x64
```

Restart Rack to load a new build. Disable real-time antivirus scanning of the build folder if compiles are slow.

## Layout

| Path | |
|---|---|
| `src/core/` | the sequencer itself: plain C++11 with no Rack dependency, covered by `tests/` |
| `src/IndexedQuadSeq.cpp`, `src/SequencerController.cpp` | the Rack modules: params, jacks, displays, threading, undo |
| `src/Serialize.hpp` | patch format |
| `res/` | panels (generated) and button graphics |
| `scripts/make_panels.py` | generates `res/*.svg`, light and dark, with all text as paths (needs `fontTools`; edit this, not the SVGs) |
