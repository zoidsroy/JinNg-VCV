# ER101-VCV

一個受 Orthogonal Devices ER-101 啟發的 4 軌 indexed sequencer，做成 VCV Rack 2 模組。

- 行為規格：[docs/SPEC.md](docs/SPEC.md)
- 目前進度：階段 0，工具鏈準備中

## Windows 開發環境

1. 安裝 [MSYS2](https://www.msys2.org/)，開啟 **MSYS2 MINGW64** shell，然後執行：
   ```bash
   pacman -Syu
   # 重新開啟 shell 後：
   pacman -Syu git wget make tar unzip zip mingw-w64-x86_64-gcc mingw-w64-x86_64-gdb mingw-w64-x86_64-cmake autoconf automake libtool mingw-w64-x86_64-jq python zstd mingw-w64-x86_64-pkgconf
   ```
2. 下載 [Rack SDK（Windows x64）](https://vcvrack.com/downloads/Rack-SDK-latest-win-x64.zip)，解壓縮到 `C:\GitWorkspace\Rack-SDK`。
3. 在 MINGW64 shell 中：
   ```bash
   cd /c/GitWorkspace/ER101-VCV
   export RACK_DIR=/c/GitWorkspace/Rack-SDK
   make install
   ```
   `make install` 會把外掛裝到 `%LOCALAPPDATA%\Rack2\plugins-win-x64`，重新啟動 Rack 就能載入。這台電腦上裝的是 Rack 2 Pro，Pro 版和 Free 版用同一個使用者資料夾。

Build 時請暫時關閉防毒軟體的即時掃描，否則編譯會非常慢。
