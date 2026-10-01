# 手動安裝（上架 VCV Library 之前）

[English](INSTALL.md) | 繁體中文

在這些模組上架 [VCV Library](https://library.vcvrack.com) 之前，可以依照以下步驟手動安裝。需要 VCV Rack 2（免費版或 Pro 版皆可，建議 2.6 以上）。

## 安裝預先編譯好的外掛

1. 打開 [Releases 頁面](https://github.com/zoidsroy/JinNg-VCV/releases/latest)，下載適合你電腦的檔案：

   | 電腦 | 檔案 |
   |---|---|
   | Windows | `JinNg-<版本>-win-x64.vcvplugin` |
   | Apple 晶片的 Mac（M1 以後） | `JinNg-<版本>-mac-arm64.vcvplugin` |
   | Intel 處理器的 Mac | `JinNg-<版本>-mac-x64.vcvplugin` |
   | Linux | `JinNg-<版本>-lin-x64.vcvplugin` |

   不確定 Mac 該下載哪一個的話，可以先看第 4 步：Rack 建立的 `plugins-…` 資料夾名稱就是答案。
2. 關閉 Rack。
3. 打開 Rack 的使用者資料夾。在 Rack 裡選 **Help → Open user folder**，或直接前往：
   - Windows：`%LOCALAPPDATA%\Rack2`（貼到檔案總管的網址列）
   - macOS：`~/Library/Application Support/Rack2`（在 Finder 選「前往 → 前往檔案夾⋯」）
   - Linux：`~/.local/share/Rack2`
4. 把下載的 `.vcvplugin` 檔案複製到裡面的 `plugins-…` 資料夾（`plugins-win-x64`、`plugins-mac-arm64`、`plugins-mac-x64` 或 `plugins-lin-x64`）。檔案保持原樣，不要解壓縮或改名。
5. 啟動 Rack。Rack 啟動時會自動把檔案解開成 `JinNg` 資料夾。
6. 在機架的空白處按右鍵，搜尋 **SEQ-101**。要用擴充模組的話，把 **SEQ-101ext** 放在它的正右邊。

Pro 版的外掛（VST/AU/CLAP）使用同一個使用者資料夾，所以這樣裝完那邊也能用。

macOS 和 Linux 的檔案是在 GitHub 上自動編譯的，通過了和 Windows 版相同的單元測試，但還沒有在這兩個系統的 Rack 裡實際試用過。如果遇到問題，歡迎[回報 issue](https://github.com/zoidsroy/JinNg-VCV/issues)。

## 從原始碼編譯

如果想自己編譯：

1. 安裝編譯器和建置工具：
   - **Windows**：[MSYS2](https://www.msys2.org/)，步驟見 [README](../README.md#building)。
   - **macOS**：執行 `xcode-select --install`，再用 [Homebrew](https://brew.sh) 執行 `brew install jq zstd`。
   - **Ubuntu/Debian**：`sudo apt install build-essential git jq zstd unzip curl`。
2. 下載適合你平台的 [Rack SDK](https://vcvrack.com/downloads/)（Mac 請選和晶片相符的 `mac-arm64` 或 `mac-x64`），解壓縮到原始碼資料夾的旁邊，讓它的位置是 `../Rack-SDK`。
3. 下載原始碼並編譯：

   ```bash
   git clone https://github.com/zoidsroy/JinNg-VCV.git
   cd JinNg-VCV
   make install
   ```

   `make install` 會編譯外掛，並把 `.vcvplugin` 複製到 Rack 的外掛資料夾。SDK 放在別的地方的話，加上 `RACK_DIR=/SDK的路徑`。
4. 重新啟動 Rack。

## 更新

用新版的檔案重複上面的步驟即可。Rack 啟動時會用新版取代已解開的 `JinNg` 資料夾。

模組上架 VCV Library 之後，也可以改從 Library 加入。Library 版使用相同的外掛 ID（`JinNg`），所以你存的 patch 都能照常開啟。Library 一有較新的版本，Rack 的自動更新就會取代手動安裝的那份。

## 移除

關閉 Rack，然後從 `plugins-…` 資料夾刪除 `JinNg` 資料夾和所有 `JinNg-*.vcvplugin` 檔案。

## 疑難排解

- **找不到模組。** 確認檔案直接放在正確的 `plugins-…` 資料夾裡（不是在子資料夾裡），而且有重新啟動 Rack。Rack 會把跳過外掛的原因寫在使用者資料夾的 `log.txt`，在裡面搜尋 `JinNg` 就能找到。
- **`log.txt` 裡出現「Could not load plugin」或版本錯誤。** 把 Rack 更新到最新的 2.x 版。
