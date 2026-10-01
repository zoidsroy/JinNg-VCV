# Installing without the VCV Library

Until these modules are in the [VCV Library](https://library.vcvrack.com), you can install them by hand. You need VCV Rack 2 (Free or Pro; version 2.6 or later recommended).

## Install the prebuilt plugin

1. Open the [Releases page](https://github.com/zoidsroy/JinNg-VCV/releases/latest) and download the file for your computer:

   | Computer | File |
   |---|---|
   | Windows | `JinNg-<version>-win-x64.vcvplugin` |
   | Mac with Apple Silicon (M1 or later) | `JinNg-<version>-mac-arm64.vcvplugin` |
   | Mac with an Intel processor | `JinNg-<version>-mac-x64.vcvplugin` |
   | Linux | `JinNg-<version>-lin-x64.vcvplugin` |

   If you're not sure which Mac file you need, look at step 4: the name of the `plugins-…` folder Rack created tells you.
2. Quit Rack.
3. Open Rack's user folder. In Rack, choose **Help → Open user folder**, or go to:
   - Windows: `%LOCALAPPDATA%\Rack2` (paste it into the File Explorer address bar)
   - macOS: `~/Library/Application Support/Rack2` (in Finder, **Go → Go to Folder…**)
   - Linux: `~/.local/share/Rack2`
4. Copy the downloaded `.vcvplugin` file into the `plugins-…` folder in there (`plugins-win-x64`, `plugins-mac-arm64`, `plugins-mac-x64` or `plugins-lin-x64`). Leave it as it is; don't unpack or rename it.
5. Start Rack. It unpacks the file into a `JinNg` folder on startup.
6. Right-click an empty part of the rack and search for **SEQ-101**. Put **SEQ-101ext** directly to its right if you want the expander.

The Pro plugin (VST/AU/CLAP) uses the same user folder, so this also installs the modules there.

The macOS and Linux files are built automatically on GitHub and pass the same unit tests as the Windows build, but haven't been tried in Rack on those systems yet. If something doesn't work, please [open an issue](https://github.com/zoidsroy/JinNg-VCV/issues).

## Building from source

To build it yourself instead:

1. Install a compiler and the build tools:
   - **Windows**: [MSYS2](https://www.msys2.org/), as in the [README](../README.md#building).
   - **macOS**: `xcode-select --install`, then `brew install jq zstd` with [Homebrew](https://brew.sh).
   - **Ubuntu/Debian**: `sudo apt install build-essential git jq zstd unzip curl`.
2. Download the [Rack SDK](https://vcvrack.com/downloads/) for your platform (on a Mac, the one for your chip: `mac-arm64` or `mac-x64`) and unpack it next to where you'll clone the source, so it ends up as `../Rack-SDK`.
3. Clone the source and build it:

   ```bash
   git clone https://github.com/zoidsroy/JinNg-VCV.git
   cd JinNg-VCV
   make install
   ```

   `make install` builds the plugin and copies the `.vcvplugin` into Rack's plugin folder. If the SDK is elsewhere, add `RACK_DIR=/path/to/Rack-SDK`.
4. Restart Rack.

## Updating

To update, repeat the steps above with the new file. Rack replaces the unpacked `JinNg` folder with the new version when it starts.

Once the modules are in the VCV Library, you can add them from there instead. The Library version uses the same plugin ID (`JinNg`), so your patches keep working. Rack's own update replaces the hand-installed copy as soon as the Library has a newer version.

## Uninstalling

Quit Rack, then delete the `JinNg` folder and any `JinNg-*.vcvplugin` file from the `plugins-…` folder.

## Troubleshooting

- **The modules don't show up.** Check that the file is directly in the right `plugins-…` folder, not in a subfolder, and that you restarted Rack. Rack writes the reason it skipped a plugin to `log.txt` in the user folder; search it for `JinNg`.
- **"Could not load plugin" or a version error in `log.txt`.** Update Rack to the latest 2.x.
