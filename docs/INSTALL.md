# Installing without the VCV Library

Until these modules are in the [VCV Library](https://library.vcvrack.com), you can install them by hand. You need VCV Rack 2 (Free or Pro; version 2.6 or later recommended).

## Windows: install the prebuilt plugin

1. Open the [Releases page](https://github.com/zoidsroy/JinNg-VCV/releases/latest) and download `JinNg-<version>-win-x64.vcvplugin`.
2. Quit Rack.
3. Open Rack's user folder. Either:
   - in Rack, choose **Help → Open user folder**, or
   - paste `%LOCALAPPDATA%\Rack2` into the File Explorer address bar.
4. Copy the downloaded `.vcvplugin` file into the `plugins-win-x64` folder in there. Leave it as it is; don't unzip or rename it.
5. Start Rack. It unpacks the file into a `JinNg` folder on startup.
6. Right-click an empty part of the rack and search for **SEQ-101**. Put **SEQ-101ext** directly to its right if you want the expander.

The Pro plugin (VST/AU/CLAP) uses the same user folder, so this also installs the modules there.

## macOS and Linux: build from source

There is no prebuilt plugin for these yet, and they have not been tested. To build one:

1. Install a compiler and the build tools:
   - **macOS**: `xcode-select --install`, then install `cmake autoconf automake libtool jq python zstd pkg-config` with [Homebrew](https://brew.sh).
   - **Ubuntu/Debian**: `sudo apt install build-essential git cmake autoconf automake libtool jq python3 zstd pkg-config`.
2. Download the [Rack SDK](https://vcvrack.com/downloads/) for your platform (on a Mac, the one for your chip: `mac-arm64` or `mac-x64`) and unpack it.
3. Clone the source and build it:

   ```bash
   git clone https://github.com/zoidsroy/JinNg-VCV.git
   cd JinNg-VCV
   make RACK_DIR=/path/to/Rack-SDK install
   ```

   `make install` builds the plugin and copies the `.vcvplugin` into Rack's plugin folder:
   - macOS: `~/Library/Application Support/Rack2/plugins-mac-arm64` (or `plugins-mac-x64`)
   - Linux: `~/.local/share/Rack2/plugins-lin-x64`
4. Restart Rack.

If the build fails, please [open an issue](https://github.com/zoidsroy/JinNg-VCV/issues) with the error output.

## Updating

To update, repeat the steps above with the new file. Rack replaces the unpacked `JinNg` folder with the new version when it starts.

Once the modules are in the VCV Library, you can add them from there instead. The Library version uses the same plugin ID (`JinNg`), so your patches keep working. Rack's own update replaces the hand-installed copy as soon as the Library has a newer version.

## Uninstalling

Quit Rack, then delete the `JinNg` folder and any `JinNg-*.vcvplugin` file from the `plugins-…` folder.

## Troubleshooting

- **The modules don't show up.** Check that the file is directly in the right `plugins-…` folder, not in a subfolder, and that you restarted Rack. Rack writes the reason it skipped a plugin to `log.txt` in the user folder; search it for `JinNg`.
- **"Could not load plugin" or a version error in `log.txt`.** Update Rack to the latest 2.x.
