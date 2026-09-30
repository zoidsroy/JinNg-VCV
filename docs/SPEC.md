# ER-101 風格 Indexed Quad Sequencer — VCV Rack 模組規格

> 依據：Orthogonal Devices《ER-101 User Manual, Firmware v2.09》（2018-08-20）。
> 本文件是行為規格的整理與改寫，不是手冊的翻譯。標示 **[未定]** 的項目是手冊沒寫清楚、需要實機或社群資料確認的地方。
> 標示 **[VCV]** 的項目是移植到 Rack 時必須做的設計決定。

---

## 0. 命名與授權

- 「Orthogonal Devices」「ER-101」是原廠名稱。模組的名稱、slug、面板都要用自己的，例如 slug `IndexedQuadSeq`，面板上不放原廠 logo，也不照抄原廠的面板排版。
- 行為可以參考原機，但程式碼要從頭寫，不能使用原廠韌體或 ER-101 Programmer 的程式碼。
- 如果之後要上 VCV Library，描述裡可以寫「inspired by」，但不能寫成官方產品。

---

## 1. 總覽

- 外部 clock 驅動、可以 reset 的 4 軌步進音序器，每軌有三路輸出：**CV-A**、**CV-B**、**GATE**。
- 「Indexed」的意思是：step 不直接存電壓，只存 0–99 的**索引**，實際電壓從該軌的**電壓表**查出來。
- 原機寬度 26HP。

---

## 2. 資料模型

```
Module
├─ tracks[4]
│   ├─ patterns: list (≤100)
│   │   ├─ steps: list (≤100)
│   │   │   └─ Step { cvA, cvB, duration, gate : 0..99 ; smoothA, smoothB, ratchet : bool }
│   │   └─ smoothA, smoothB : bool          // pattern 層的 smooth
│   ├─ smoothA, smoothB : bool              // track 層的 smooth
│   ├─ tableA[100], tableB[100] : 電壓 0.000–8.192V（以 mV 整數儲存）
│   ├─ loopStart, loopEnd : 可為空，指向某個 step
│   ├─ options { displayA, displayB : Note|Number ; clockDiv 1..99 ; clockMul 1..99 ; gateMode : Gate|Trigger }
│   ├─ math { cvA, cvB, duration, gate : MathOp }   // 每軌各自保存
│   └─ voltageEditGranularity : Fine|Coarse|SuperCoarse   // 會存進 snapshot
├─ snapshots[16]         // 另有一個 '--' 空白 snapshot，載入它等於清空
├─ userRefTables[8]      // 使用者的參考電壓表
└─ 執行期狀態：playCursor[4]、editCursor[4]、clipboard、mode、paused、holdShadow …
```

### 容量限制
- 每軌最多 100 個 pattern，每個 pattern 最多 100 個 step。
- **四軌加總最多 2000 個 step**，先用先得。插入時如果會超過上限，就拒絕並顯示錯誤。**[VCV]** 可以把上限做成選項，不過預設要跟原機一樣。

### 新插入 step 的初始值
- cvA 和 cvB 取「在表 A／表 B 中最接近 1.0V 的那個索引」。用 12ET 表時就是 12。
- duration = 0，所以新 step 一開始會被跳過。
- gate = 0。

### 預設電壓表
- 軌道電壓表預設是 12ET：索引 *i* 對應 *i*/12 V。所以索引 12 = 1.000V，索引 96 = 8.000V，索引 97–99 會被限制在 8.192V 以內。**[未定]** 12ET 表中 ≥ 8.192V 的索引，實際值要再確認。
- 內建參考表有 8 張：
  | 名稱 | 內容 |
  |---|---|
  | 12ET | 12 平均律（半音） |
  | 24ET | 24 平均律（四分音） |
  | 22JT | 22 音純律（印度 22 shruti） |
  | BLUE | 自然音階加上降三、降七音 |
  | PEnt | 0–49 為大調五聲音階，50–99 為小調五聲音階 |
  | L-8 | 線性 0–8.0V，每格 100mV |
  | E-8 | 指數曲線 0–8.0V |
  | LE-8 | 0–0.1V 線性（每格 2mV），接著 0.1–0.8V 指數曲線 |
- **[未定]** 22JT、BLUE、PEnt、E-8、LE-8 的精確數值手冊沒有列出。可以從 ER-101 Programmer 匯出的 XML 取得，或自己推導後標註「近似值」。

---

## 3. 時序（播放引擎的核心）

### 3.1 Clock
- CLOCK 輸入的上升沿會讓四軌的 play cursor 前進一個 pulse。原機閾值約 2.5V，低電位 < 1.5V，高電位 > 3.5V。**[VCV]** 用 `dsp::SchmittTrigger`，閾值可以直接照原機設 1.5/3.5V，或改用 Rack 慣用的較低閾值，需要決定。
- PAUSE 時忽略 clock。PAUSE 期間 GATE 輸出固定為低。
- 每軌再各自經過 clock 除頻與倍頻（見 3.6），得到該軌的內部 pulse。

### 3.2 Step 的播放
- step 長度是 `duration` 個 pulse。
- **duration = 0 的 step 會被直接跳過**，不佔時間。**[VCV]** 如果整軌（或 loop 範圍內）所有 step 的 duration 都是 0，要避免無窮迴圈：停在當前 step，不前進。
- Gate 模式、沒有 ratchet 時：
  - 從 step 開始算起，gate 高 `min(gate, duration)` 個 pulse，之後變低。
  - gate = 0 是休止；gate ≥ duration 是 legato，整個 step 都是高。
  - **[未定]** 連續兩個 legato step 之間，gate 會不會有一個短暫的低電位讓 EG 重新觸發？手冊沒說。建議先做成完全不中斷，重新觸發留作選項。

### 3.3 Ratchet（note repeat）
- Gate 模式：gate 以「高 `gate` 個 pulse、低 `gate` 個 pulse」為週期重複，直到 step 結束。例如 duration 8、gate 2，會觸發 2 次；gate 1 會觸發 4 次。
- Trigger 模式：每次 trigger 固定 0.5ms，gate 值代表這個 step 內要觸發的**次數**。**[未定]** 次數在 step 內如何分布手冊沒寫，先假設平均分布在 step 長度內。

### 3.4 Trigger 模式（軌道選項 gateMode = Trigger）
- 每個 step 的 gate 輸出變成一個短脈衝，長度 = gate × 0.1ms，範圍 0.1–9.9ms。**[VCV]** 以取樣時間計算，不依賴 clock。
- Trigger 模式下，smooth 的長度改為 step 長度的 50%（見 3.5）。

### 3.5 Smooth（平滑轉換）
- smooth 旗標在 step、pattern、track 三層各自設定，而且 A 和 B 分開。只要任一層開啟，該 step 的轉換就是平滑的。
- 平滑的意思是：從這個 step 的電壓線性滑到**下一個**step 的電壓。
- 滑動從 gate 變低的時候開始，到下一個 step 開始時結束，也就是時間長度為 `duration − gate` 個 pulse。
- **[VCV]** 滑動需要把「pulse」換算成秒：用最近量到的 clock 週期（每軌除頻、倍頻之後的週期）乘上 pulse 數，在每個 sample 內插。這也是手冊說的「滑動時間會即時跟著 tempo 調整」。
- 從手冊的 LFO 範例可以推得：gate = 0 時，整個 step 都在滑動。duration = 0 的 step 如果開了 smooth，會變成瞬間跳到下一個電壓（鋸齒波範例就是這樣做的）。
- **[未定]** 「下一個 step」是指跳過 duration=0 之後的下一個，還是字面上的下一個 step？另外，loop 回頭時的下一個 step 是 loop start 嗎？以鋸齒波範例來看，duration=0 的 step 仍然提供了起點電壓，所以應該是字面上的下一個 step，並考慮 loop。實作時要寫測試把這個行為固定下來。

### 3.6 Clock 除頻與倍頻（每軌）
- 除頻 N：外部 pulse 編號 1, 2, 3…，只讓第 1, 1+N, 1+2N… 個通過。
- 倍頻 M：外部 pulse 到達時，立刻送出一個 pulse，並記錄與上一個外部 pulse 的間隔 T。之後每 T/M 送出一個，總共 M 個。如果中途有新的外部 pulse 到來，就中止並重新開始。這樣倍頻出來的 clock 一定跟外部 clock 同相，但週期會落後一個 pulse 才更新。
- **[未定]** 同時設定除頻與倍頻時，先除再倍，還是先倍再除？推測是先除頻，再對除頻後的 clock 倍頻。

### 3.7 Reset
- RESET 輸入的上升沿（或按下 RESET 按鈕），會讓四軌都回到第 1 個 pattern 的第 1 個 step，不管 loop 設定。
- **RESET 保持高電位時，音序器停在第一個 step 並忽略 clock**，直到 RESET 變低。
- 量化 reset：按住 TRACK／PATTERN／STEP 的 focus 按鈕再按 RESET，reset 會等到目前（被 focus 的那一軌）的 track、pattern 或 step 結束時才發生。
- **[未定]** reset 之後的第一個 clock，是播放第一個 step，還是前進到第二個 step？這決定是否需要「reset 後的 clock 不前進」的處理。**[VCV]** 另外要處理 Rack 常見的狀況：reset 和 clock 在同一個 sample 到達。慣例是 reset 後約 1ms 內忽略 clock。

### 3.8 Loop
- 每軌各自有 loop start 和 loop end，各自可以不設定：
  - 都沒設：整軌播完後，從頭開始。
  - 只設 start：播到結尾後，回到 start。
  - 只設 end：播到 end 後，回到第一個 step。
  - 都有設：在 start 和 end 之間循環。start = end 時，只循環那一個 step。
- **[VCV]** loop 點要存成「指向某個 step」的形式。插入或刪除 step 時要一起更新；刪除到 loop 點本身時，要決定清掉還是移到相鄰的 step。**[未定]**

### 3.9 輸出
- CV 範圍是 0–8.192V。原機是 14-bit DAC，每 1mV 為一格。**[VCV]** 預設輸出不做量化的連續電壓；可以加一個選項模擬 14-bit 階梯。
- GATE 輸出：低 0V，高 10V（原機約 9V 以上，Rack 慣例是 10V）。
- 原機的輸出更新率約 3kHz。**[VCV]** 可以在每個 sample 更新，或每 N 個 sample 更新一次以節省 CPU；兩種方式聽感差異不大。

---

## 4. 操作介面與模式

### 4.1 面板元件
- 左半部是導航顯示：INDEX、TRACK、PATTERN、STEP、SNAPSHOT。右半部是參數顯示：VOLTAGE、CV-A、CV-B、DURATION、GATE。
- 每個顯示旁邊有一個 focus 按鈕（附 LED）。左右各有一個**無段旋鈕（encoder）**：左旋鈕改導航值，右旋鈕改參數值。
- 其他按鈕：INSERT、DELETE、MATH、COPY、LOAD、SAVE、LOOP START、LOOP END、SMOOTH、COMMIT、PAUSE、RESET。
- 三段切換開關：MODE（edit／hold／follow）、TABLE（A／B／ref）。
- 輸入只有 CLOCK 和 RESET。原機**沒有** CV 調變輸入。
- **[VCV]** 要做 4 位數的 7 段顯示器 widget、encoder widget（用沒有上下限的旋鈕讀取變化量），以及可以偵測「按住」「連按兩下」「在已 focus 的狀態下再按一次」的按鈕邏輯。

### 4.2 「focus 按下」的慣例
- 對已經 focus 的顯示再按一次 focus 按鈕，會觸發次要功能：
  - TRACK、PATTERN：在 VOLTAGE 顯示中顯示總 pulse 數；TRACK 的 focus 按下還會進入軌道選項畫面（見 4.8）。**[未定]** 這兩個行為在手冊中的描述有衝突，需要確認。
  - GATE：切換 ratchet。
  - VOLTAGE：切換微調、粗調、超粗調。

### 4.3 三種模式
- 每軌有兩個獨立的游標：play cursor（決定輸出）和 edit cursor（決定顯示與編輯位置）。
- **EDIT**：顯示 edit cursor。編輯會立即生效，下一次 play cursor 播到那裡時就聽得到。
- **FOLLOW**：顯示 play cursor，而且它會自己前進。左旋鈕可以拖動 play cursor（scrub）。沒有 PAUSE 時禁止 INSERT、DELETE 和修改 step 參數，嘗試時會閃「TILt」；但允許設定 loop 和 smooth。
- **HOLD**：進入時複製一份所有軌道的完整狀態（shadow）。之後的編輯都在這份副本上進行，播放不受影響。按 COMMIT 才把副本寫回播放中的資料：
  - 量化 commit：focus 在 TRACK、PATTERN 或 STEP 時按 COMMIT，會等到目前 focus 那一軌的 track、pattern 或 step 結束才寫回。等待期間 COMMIT LED 閃爍。
  - 立即 commit：連按兩次 COMMIT，或在 focus 為 INDEX 或 SNAPSHOT 時按。
  - 離開 HOLD 而沒有 commit，會丟棄副本。
  - 在 HOLD 中載入 snapshot，也可以用 COMMIT 對拍切入。

### 4.4 編輯：插入、刪除、複製
- INSERT（剪貼簿為空時）：依 focus 插入一個新的 step，或一個新的空 pattern。
  - 按住 INSERT 再轉右旋鈕，可以選擇 `AFtr`（插在後面，預設）、`SPLt`（分割）、`bEFr`（插在前面），放開按鈕時才執行。
  - 分割 step：把 duration 平分，總長不變；奇數時前面多 1。例如 5 會分成 3 + 2。
  - 分割 pattern：在游標位置切開。
- DELETE：刪除 focus 中的 step 或 pattern。focus 在 TRACK 時會清空整軌，需要再按一次 DELETE 確認，按其他鍵則取消。
- COPY：把 focus 中的 step、pattern 或 track 複製到剪貼簿，COPY LED 會亮起。之後的 INSERT 變成貼上。再按一次 COPY 會清空剪貼簿。
  - 按住 COPY 同時轉左旋鈕，可以選取一段連續的 step 或 pattern。
  - 剪貼簿裡是 step，但 focus 在 PATTERN 時，INSERT 會插入新的空 pattern，而不是貼上；反之亦然。
  - 複製 track 會連同軌道選項一起複製。
- DURATION 的 swing（移動 step 邊界）：按住 DURATION 按鈕轉右旋鈕，這個 step 增加的 pulse 數會從下一個 step 扣掉，兩者總和不變。

### 4.5 電壓表操作
- TABLE 開關選 A 或 B：INDEX 顯示為索引 0–99（左旋鈕），VOLTAGE 顯示該索引的電壓。focus 在 VOLTAGE 時可以用右旋鈕修改電壓。
- 調整精度：
  | 等級 | 數字顯示 | 音名顯示 |
  |---|---|---|
  | 微調 | ±2mV | ±1% 全音 |
  | 粗調 | ±100mV | 相鄰半音 |
  | 超粗調 | ±1V | ±1 八度 |
- 音名顯示格式為 `八度.音名.全音百分比`。例如 C2 顯示為 `2.C.00`，A#3 顯示為 `3.A.50`，G 加四分之一音顯示為 `4.G.25`。從 Quick Start 可以推得 1.000V 顯示為 `1.C.00`，也就是 n V 對應 `n.C.00`。注意：手冊圖 3 寫的「0V 為 B」是那個範例中 VCO 的調音方式，不是顯示格式的定義。升降音以「下方自然音 + 50」表示，例如 C# 顯示為 `C.50`。因為 E–F、B–C 只差半音，所以 E 和 B 的百分比最多只會到 49。
- TABLE 開關切到 ref：右旋鈕選參考表，左旋鈕瀏覽索引。
- 複製電壓表：focus 在 INDEX 時按 COPY，選好目的地表，再按 INSERT 覆蓋。來源與目的地都可以是軌道表或使用者參考表，內建表不可覆寫。

### 4.6 Math 運算
- 每軌有一組 math 設定，cvA、cvB、duration、gate 四個參數各有一個運算和一個運算元：
  | 代碼 | 運算 |
  |---|---|
  | A | 加（運算元可為負） |
  | G | 乘或除 |
  | S | 設定為常數 |
  | rd | 設為 0..N 的隨機值 |
  | Jt | 加上 −N..N 的隨機值 |
  - **[未定]** 手冊還提到一個「量化到 N 的倍數」運算，但操作說明的循環順序（A→G→S→rd→Jt）裡沒有它，兩處說法矛盾。
- 按下 MATH，會把目前的 math 設定套用到 focus 中的 step、pattern 或整軌。
- **結果若超出 0–99，該參數保持原值不變**，不會截斷到邊界。
- 編輯 math 設定：按住 MATH 時可以直接調整，放開就套用；也可以按 VOLTAGE 把編輯畫面固定住（pin）。按住 MATH 時按 DELETE，會恢復成恆等運算。
- math 設定會存進 snapshot。

### 4.7 Snapshot
- 16 個儲存槽，外加一個 `--` 空白槽，載入空白槽等於清空全部資料（但保留已儲存的 snapshot）。
- SAVE 和 LOAD 都要按兩次確認，第一次按會閃「Abrt」提示，按其他鍵取消。
- 載入 snapshot 時，所有 play cursor 和 edit cursor 都回到開頭，但 focus 的軌道不變。
- **[VCV]** 目前的完整狀態存在 patch 裡（`dataToJson`）。16 個 snapshot 的位置要決定：
  - 方案 1：跟著 patch 存。patch 是自給自足的，推薦用這個。
  - 方案 2：存在使用者資料夾，所有 patch 共用，比較接近原機「存在快閃記憶體」的感覺。
  - 使用者參考表（8 張）比較適合存在使用者資料夾，所有 patch 共用。
- **[VCV]** 原機 SAVE 時會卡住約 0.75 秒，這個行為不需要模擬。
- 選配功能：匯入、匯出 ER-101 Programmer 的 XML snapshot 格式。需要先取得格式樣本。

### 4.8 軌道選項畫面
| 顯示位置 | 選項 | 範圍 |
|---|---|---|
| CV-A | A 的顯示方式 | Nt（音名）／Nr（數字） |
| CV-B | B 的顯示方式 | Nt／Nr |
| DURATION | clock 除頻 | 1–99 |
| STEP | clock 倍頻 | 1–99 |
| GATE | gate 或 trigger 模式 | Gt／tr |

---

## 5. VCV Rack 實作架構

### 5.1 執行緒模型（最重要的設計決定）
- `process()` 跑在 audio engine 執行緒；面板操作跑在 UI 執行緒。兩邊都要讀寫序列資料，**不能用鎖**，因為鎖可能造成 audio 斷音。
- 建議做法：
  - UI 執行緒只**送出命令**（例如 `InsertStep{track, pos, mode}`），放進一個 lock-free 的單一生產者、單一消費者佇列。
  - engine 執行緒在 `process()` 開頭取出命令並套用。所有資料只在 engine 執行緒上修改。
  - 顯示所需的狀態由 engine 執行緒定期寫到一個 double buffer，UI 執行緒只讀取。
- 好處：HOLD 模式的 shadow copy 與量化 commit 都在 engine 執行緒上完成，時序可以精確到 sample；Rack 的 undo（`history`）也可以用同一組命令來實作。

### 5.2 模組分層
```
src/
  core/           // 純 C++，不依賴 Rack，可以單獨寫單元測試
    Sequence.hpp      資料模型 + 2000 step 上限
    Playhead.hpp      每軌的播放狀態機（duration/gate/ratchet/smooth/loop）
    ClockDivMul.hpp   除頻、倍頻
    VoltageTables.hpp 內建參考表與 12ET 產生
    MathOps.hpp
    Commands.hpp      編輯命令與套用邏輯
    Serialize.hpp     JSON（以及未來的 XML 匯入）
  ui/
    SegmentDisplay.hpp  7 段 LED 顯示器
    Encoder.hpp
    UiState.hpp         focus、按鈕的按住／連按判斷、TILt/Abrt 等訊息
  IndexedQuadSeq.cpp    Module + ModuleWidget
```
- `core/` 不 include `rack.hpp`，這樣可以在不開 Rack 的情況下，用普通的測試程式驗證時序行為。這是準確重現原機行為的關鍵。

### 5.3 記憶體
- step 總數上限只有 2000，可以直接預先配置一個固定大小的 step 陣列，用索引串接，完全避免在 engine 執行緒上配置記憶體。HOLD 的 shadow 也預先配置一份。

---

## 6. 開發階段

| 階段 | 內容 | 完成條件 |
|---|---|---|
| 0 | 工具鏈：MSYS2、Rack SDK、Rack 2 Free；用 `helper.py` 建立空白模組並成功 build | Rack 中看得到空白模組 |
| 1 | `core/` 播放引擎＋單元測試：step、duration、gate、跳過 0 duration、loop、reset | 測試涵蓋第 3 章所有規則 |
| 2 | 最小可用模組：4 軌輸出；先用右鍵選單或簡單表格編輯 step；存進 patch | 在 Rack 中可以實際演奏 |
| 3 | smooth、ratchet、trigger 模式、除頻與倍頻 | 手冊 Tips 的三角波、鋸齒波、正弦波 LFO 範例輸出正確 |
| 4 | 原機式介面：顯示器、encoder、focus、INSERT/DELETE/COPY、電壓表編輯 | 能照手冊 Quick Start 的步驟操作 |
| 5 | EDIT/FOLLOW/HOLD、量化 commit、量化 reset、math、snapshot、參考表 | 手冊所有章節的流程都可以操作 |
| 6 | 面板美術、手冊、上架準備（自有名稱） | — |

---

## 7. 待確認問題彙整

1. reset 之後的第一個 clock，是播放 step 1 還是前進到 step 2？（3.7）
2. smooth 的「下一個 step」怎麼認定：duration=0 的 step 算不算、loop 回頭時怎麼算？（3.5）
3. 連續 legato step 之間 gate 是否會中斷？（3.2）
4. Trigger 模式加 ratchet 時，多次觸發的時間分布方式。（3.3）
5. 除頻與倍頻同時設定時的處理順序。（3.6）
6. 刪除 loop 點所在的 step 時，loop 點怎麼處理。（3.8）
7. 內建參考表 22JT、BLUE、PEnt、E-8、LE-8 的精確數值；12ET 中超過 8.192V 的索引。（2）
8. 「量化」math 運算是否存在於 v2.09 韌體。（4.6）
9. TRACK 的 focus 按下，到底是顯示總 pulse 數還是進入軌道選項。（4.2）
10. ER-102 擴充模組：這份手冊沒有涵蓋。如果要支援，需要另外找 ER-102 的手冊。

有實機、官方論壇討論串或 ER-101 Programmer 匯出的 XML，就能回答大部分的問題。
