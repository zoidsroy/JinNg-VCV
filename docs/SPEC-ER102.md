# ER-102 風格擴充模組 — VCV Rack 規格

> 依據：Orthogonal Devices《ER-102 User Manual, Firmware v2.04》（2016-12-13），以及手冊第 8 頁的面板圖。
> 本文件是行為規格的整理與改寫，不是手冊的翻譯。標示方式沿用 [SPEC.md](SPEC.md)：**[未定]** 表示手冊沒寫清楚；**[VCV]** 表示移植到 Rack 時的設計決定。
> 命名原則同 ER-101：模組叫 `Sequencer Controller`（slug `SequencerController`），面板不放原廠名稱或 logo。

---

## 1. 總覽

ER-102 是 ER-101 的擴充器，放在 ER-101 的**右邊**。它本身沒有旋鈕，所有數值都用 ER-101 的左右旋鈕調整：
- 左旋鈕調整「focus 鈕在顯示器右邊」的項目：PART、GROUP。
- 右旋鈕調整「focus 鈕在顯示器左邊」的項目：GROUP MODIFIERS。
- 需要顯示數值時，會借用 ER-101 的 INDEX 和 VOLTAGE 顯示器。

面板是 14HP，由上而下分成四區：
| 區塊 | 功能 |
|---|---|
| PARTS | 每個 snapshot 最多 99 個 part，每個 part 對 4 軌各記一組 RESET TO 步與 loop 區段，可以用 CV 切換 |
| STORAGE | 原機是 microSD 卡：snapshot 存取、使用者電壓表、MIDI 匯入、韌體更新 |
| GROUPS | 最多 16 個 group，每個 group 是任意選取的 step 集合，各有破壞性與非破壞性運算，並接到 X/Y/Z 三路 CV/gate 調變匯流排 |
| RECORDING | 三種錄音模式（real-time、step、alter），6 個多用途輸入，加上 PUNCH IN/OUT |

接上 ER-102 之後，ER-101 的 MATH 也會升級成完整版的運算（見第 2 章）。

---

## 2. MATH 運算（接上 ER-102 時）

每個 step 參數（CV-A、CV-B、DURATION、GATE）各有一組 5 種運算，而且**同時生效**：
| 代碼 | 運算 | 範圍 |
|---|---|---|
| A / S | 加法；G=0 時變成設定為 S | −99..99 |
| G | 乘法 | 1/99..99（含 0） |
| Jt | 加上 [−Jt, Jt] 的隨機整數 | 0..99 |
| Rd | 隨機 [0, Rd] | 0..99 |
| Qt | 四捨五入到 Qt 的倍數 | 1..99 |

- 破壞性運算（MATH 鍵，直接改寫 step）：`P' = Q( G × (Rd>0 ? RANDOM(Rd) : P) + JITTER(Jt) + A , Qt )`
- 非破壞性運算（group 的 high/low，只在播放時作用）：`P' = Q( G × (P + RANDOM(Rd)) + JITTER(Jt) + A , Qt )`
  - 每次播到那個 step 時，隨機值都會重新產生。
- 結果超出該參數的範圍時，保持原值。
- 編輯畫面的操作：左旋鈕選運算，右旋鈕改數值，右側的 focus 鈕選參數，DELETE 還原成恆等運算。
- INVERT 鍵：反轉目前正在編輯的運算，只影響 A 和 G。按住 INVERT 再按 MATH，會套用反轉後的運算。
- **[VCV]** 資料統一用這個 5 運算模型。ER-101 單獨使用時的 5 種單一運算都能用它表示：
  | ER-101 單一運算 | 等價的 5 運算設定 |
  |---|---|
  | 加 n | A=n |
  | 乘 n | G=n |
  | 除 n | G=1/n |
  | 設定 n | G=0、A=n |
  | 隨機 | Rd |
  | 抖動 | Jt |

  所以只需要一種資料格式，另外準備兩種編輯介面：沒接 ER-102 時用 ER-101 的單一運算介面，接上時用完整介面。
- **[已實作，E1，改變了上面的決定]** 實作時改成兩種運算**各存一份**：每軌有 ER-101 的單一運算（`math`）和 ER-102 的 5 運算（`transform`），MATH 依照有沒有接 ER-102 決定用哪一份。原因是兩者互相轉換時有歧義：例如 G=0，在 ER-101 的單一運算裡代表「不變」，在 5 運算裡代表「設定」。
  - G 的右旋鈕順序：0、/99 … /2、1、2 … 99。左欄代碼顯示方向：`-A` 代表減，`-G` 代表除；G=0 時 `A` 改成 `S`。
  - 5 運算的 MATH 畫面：右側 focus 鍵選參數，左側 TRACK、PATTERN、STEP、SNAPSHOT 四行也可以直接選對應的參數；左旋鈕選運算，右旋鈕改值。
  - INVERT 和 ROTATE 放開時才動作。按住期間如果按了別的鍵（ROTATE 或 MATH），就只當修飾鍵用。
- **[未定]** G 可以設成 1/99..99。手冊沒有交代分數要怎麼顯示、右旋鈕要怎麼步進。

---

## 3. Parts

### 3.1 資料
- 每個 snapshot 有 part 1..99，外加內建的 part 0（STOP：四軌全部不發聲）。
- 每個 part 對每一軌記錄三個值：RESET TO 步、LOOP START、LOOP END，每個都可以不設定。
- 接上 ER-102 時，**ER-101 的 LOOP START/END 設定的是 focus 中那個 part 的 loop**。播放依照「正在播放的 part」的 loop 與 RESET TO。
- **[VCV]** 這些步的位置跟 loop 點一樣存成 flat index。插入或刪除 step 時，99 個 part 的所有位置都要一起位移。

### 3.2 三種 part 狀態
- **focused**：PART 顯示器上顯示的那個，編輯時作用在它身上。
- **pending**：已經觸發、排隊等著播放的那個。PART focus 時，INDEX 顯示器顯示它的編號，PART 的 LED 會閃。
- **playing**：正在播放的那個。PART 顯示器右下角的點代表它。
- PART focus 時，VOLTAGE 顯示器顯示 focus part 的概覽：分成 4 段對應 4 軌，每段有 3 條橫線，分別表示 RESET TO、LOOP START、LOOP END 有沒有設定。
- 按住 PART 鍵轉左旋鈕，可以快速跳位置，依序經過：第一步 → RESET TO → LOOP START → LOOP END → 最後一步。
- PART focus 時：COPY 再 INSERT 會複製 part 的設定；DELETE 會清除這個 part。

### 3.3 觸發與轉換
- 觸發方式：按 TRANSITION 鍵，或 ACTIVATE 輸入收到上升沿。
- SELECT 輸入可以用 CV 選 part：`part = floor(V × 10)`，範圍 0..99。插上線之後，左旋鈕就不能改 PART，強行轉動時 VOLTAGE 會閃 `PLUG`。
- ACTIVATE 保持高電位期間，pending part 會跟著 SELECT 的電壓即時改變，可以拿來「演奏」part。
- TRANSITION 開關決定 pending 何時變成 playing：
  - **FIRST**：任何一軌跑完它的 loop 一次就切換。
  - **LAST**：所有軌都至少跑完 loop 一次才切換。
  - **USER**：預設是立即切換、不 reset。原機可以用 CONFIG.INI 改這個行為。
- 切換時，有設定 RESET TO 的軌道會 reset 到那一步；沒設定的軌道會從目前的位置接著播（手冊稱為 naked loop）。
- **[未定]** 「跑完 loop 一次」怎麼判斷：沒設 loop 的軌道，是指整軌播完嗎？RESET TO 位在 loop 之前的軌道，要從進入 loop 之後才開始算嗎？

---

### 3.4 實作（E2）
- **資料**：每軌存 100 組 `PartPoints`（RESET TO、LOOP START、LOOP END），跟 loop 點一樣在插入、刪除時跟著位移。所以 snapshot、HOLD/COMMIT、Ctrl+Z 都自動涵蓋 part。
- **實際播放的 loop 仍然是軌道自己的 loop 點**。part 開始播放時，它的 loop 會複製過去。
  - 接上 ER-102 時，LOOP START、END、RESET TO 都作用在 focus 中的 part。
  - 如果 focus 的 part 正在播放，loop 的修改會同步寫到軌道上，立刻聽得到。
- **第一次接上 ER-102**：如果所有 part 都是空的，各軌目前的 loop 會變成 part 1，不會就此消失。
- **觸發**：按 TRANSITION 鍵，或 ACTIVATE 上升沿（> 1.5V）觸發。
  - 從 STOP 出發，或開關在 USER 時，立即切換。其中 USER 不做 reset；從 STOP 出發時，有 RESET TO 的軌道會在下一個 clock 從那一步開始。
  - FIRST 和 LAST 的切換，發生在 step 開始的那一刻：有 RESET TO 的軌道立刻從該步發聲；沒有的軌道照原本位置繼續播（naked loop）。
- **「跑完 loop 一次」的定義**（第 3.3 節的未定問題）：軌道發生繞回，也就是播到 loop END 跳回 loop START，或播到軌尾回到開頭。
  - LAST 只計算還有可播步驟的軌道，從觸發的那一刻開始計算。
  - reset 本身也算一次繞回。
- **RESET**：接上 ER-102 時，RESET 會回到播放中 part 的 RESET TO；沒設定的軌道回到第一步。
- **STOP**（part 0）：不前進、gate 全關，而且不能編輯。
- **SELECT**：`part = floor(V × 10)`，範圍 0..99。插線時轉左旋鈕會顯示 `PLUG`。ACTIVATE 保持高電位時，pending part 會跟著 SELECT 變化。
- **快速跳位**：按住 PART 轉左旋鈕，依序經過：第一步 → RESET TO → LOOP START → LOOP END → 最後一步，沒設定的會跳過。
- **COPY / INSERT / DELETE**（PART focus 時）：複製、貼上、清除 part 在四軌上的設定。貼上時，超出目標軌長度的點會被丟棄。
- **顯示**：
  - PART 顯示 focus 中的 part 編號；正在播放的那個會加上小數點。
  - PART focus 時，INDEX 閃爍顯示 pending 的 part，VOLTAGE 顯示 4 軌的概覽。
  - PART 的 LED 在有 pending 時閃爍。

## 4. Groups

### 4.1 選取
- 最多 16 個 group。**[VCV]** 每個 step 多存一個 16-bit 的 group 遮罩。這樣插入、刪除、複製 step 時，所屬的 group 會自然跟著走。
- 選取與編輯：
  - **(DE)SELECT**：把游標所在的 step 加入或移出 focus 中的 group。步屬於這個 group 時，紅燈會亮。
  - **COPY / INSERT**（GROUP focus 時）：複製選取範圍，貼上時和目標 group 取聯集。
  - **DELETE**（GROUP focus 時）：清空這個 group。
  - **INVERT**：反選。
  - **ROTATE**：選取往後移一步；按住 INVERT 再按 ROTATE 則往前移。
  - 以上這些操作只作用在 focus 中的那一軌。
- **Euclidean 選取**：focus 在 PATTERN 或 TRACK 時按 (DE)SELECT，進入 E(N,M) 模式。
  - 左旋鈕調 N，右旋鈕調 M（最大 99）；INDEX 和 VOLTAGE 顯示 `3Eu.8` 這種格式。
  - 遮罩會重複延伸到整個 pattern 或整軌。再按一次 (DE)SELECT 套用：1 的位置加入 group，0 的位置移出。
  - 剛進入時是 E(L,L)，L 為步數。DELETE 設成 E(0,L)；INDEX 鍵在 E(0,L) 與 E(L,L) 之間切換。
- GROUP focus 時：INDEX 顯示 `nS`，VOLTAGE 顯示這個 group 在目前軌道裡有幾步。

### 4.2 Group 的運算
- 每個 group 有一組破壞性運算：GROUP focus 時按 MATH，只作用在屬於這個 group 的 step。
- 每個 group 有 6 組非破壞性運算：X、Y、Z 三個通道各有 HIGH 和 LOW 各一組。
  - 每個通道的 GATE 輸入 > 1.5V 時用 HIGH 那組，否則用 LOW。沒插線視為 LOW。
  - 每次播到該 step 時自動套用，不需要按 MATH。
- **Slope 矩陣**：每個 group 對 X、Y、Z 三個通道 × 4 個參數，各有一個增益 K，範圍 −99..99。計算方式為 `P' = P + Kx·Vx + Ky·Vy + Kz·Vz`。
  - CV-A 和 CV-B 是在查完電壓表**之後**才加，單位是伏特。
  - 右旋鈕的步進依數值大小而定：|K| < 1 時每格 0.01，1..9.9 時每格 0.1，10..99 時每格 1。
- 編輯方式：GROUP MODIFIERS 的第一個開關選 high、slope 或 low，第二個選 X、Y 或 Z；再按 GROUP MODIFIERS 的 focus 鍵進入編輯。此時左旋鈕換 group，右旋鈕改值。
- CV 輸入為 ±10V。**[VCV]** 取樣率就用 Rack 的取樣率。
- **[未定]** 一個 step 同時屬於多個 group 時，套用的順序。**[VCV]** 決定依 group 編號由小到大依序套用。
- **[未定]** DURATION 和 GATE 的調變是在 step 開始時取樣一次，還是持續作用？**[VCV]** 決定：DURATION 和 GATE 在 step 開始時決定；CV-A 和 CV-B 的 slope 調變連續作用在輸出上。

---

### 4.3 實作（E3）
- **資料**：每個 step 多存一個 16-bit 的 group 遮罩。16 個 group 的設定存在 `Sequence` 層級，包括破壞性運算、X/Y/Z 各一組 high 和 low 運算、3×4 的 slope 矩陣。snapshot、HOLD 和 Ctrl+Z 都會涵蓋這些資料。
- **Euclidean 產生方式**：採用 Bjorklund 演算法的「配對」形式（Toussaint 論文的寫法）。
  - 手冊第 13 頁的 9 個範例中，有 7 個完全相同。E(5,16) 和 E(7,17) 則是手冊印的圖樣的旋轉版本。
  - 旋轉只改變起點，節奏本身不變，手冊也說遮罩可以依喜好旋轉；要調整時，可以用 ROTATE。
  - 我比較過三種 Bresenham 寫法和遞迴版 Bjorklund，都沒有比這個更貼近手冊。
- **Euclidean 模式的操作**：
  - 進入時是 E(L,L)，L 是游標所在 pattern 的步數。focus 在 TRACK 時，範圍是整軌，但 L 仍取游標所在 pattern 的長度。
  - 在這個模式中，只接受 (DE)SELECT（套用並離開）、DELETE（N 設為 0）、INDEX（N 在 0 與 M 之間切換）這三個鍵；其他按鍵會被忽略，放開時也不會觸發原本的動作。
- **Group 的 MATH**：GROUP focus 時，MATH 編輯與套用的是這個 group 自己的運算，作用在**所有軌道**上屬於此 group 的 step。至於選取類的操作（COPY、DELETE、INVERT、ROTATE），依手冊只作用在目前的軌道。
- **GROUP MODIFIERS 畫面**：按 GROUP MODIFIERS 鍵進入，再按一次離開。進入時左側 focus 自動移到 GROUP，右側 focus 則用來選參數（CV-A..GATE）。第一個開關撥在不同位置時：
  - **slope**：左旋鈕換 group，右旋鈕改 slope。
    - 右欄四個顯示器用簡短格式：整數 −9..99；小於 1 的非零值顯示 `0.` 或 `-0.`；−10 以下顯示 `--`。
    - VOLTAGE 顯示 focus 參數的精確值，例如 `-0.05`。手冊說 VOLTAGE 應該顯示 K×V，這裡改成顯示 K 本身，因為兩位數的顯示器放不下精確的 slope 值。**[偏離手冊]**
  - **high/low**：顯示方式和 5 運算的 MATH 畫面相同，左旋鈕選運算，右旋鈕改值，但不需要套用。
- **播放時**：
  - step 開始的那一刻，依序（group 0→15、通道 X→Y→Z）套用各通道的 high 或 low 運算，再加上 DURATION 和 GATE 的 slope，並四捨五入。
  - Random 和 Jitter 在每次播放時重新產生亂數。
  - CV-A 和 CV-B 的 slope 是在查完電壓表之後持續加上，輸出限制在 0–8.192V。
  - 被調變成 DURATION=0 的 step 會被跳過；可播的都跳過時就停住。
  - 不屬於任何 group 的 step 直接讀 sequence，所以編輯時可以立刻聽到修改。屬於 group 的 step，會在它下一次開始時才反映修改。
- **Gate 輸入**：> 1.5V 為 high，沒插線視為 low。CV 輸入沒插線視為 0V。

## 5. Recording

共用的操作：ARM 鍵對 focus 中的軌道切換錄音；PUNCH IN/OUT 鍵或 gate 輸入控制開始與結束；REC 燈表示正在錄音。

| 模式 | 輸入對應 | 行為 |
|---|---|---|
| real-time | A-1 → CV-A，A-2 → CV-B，AD-1 → gate（數位） | 以 clock 為基準，把演奏量化成 step，插在播放游標的位置。可以設定：CV-A/CV-B 改變時是否開新步，以及 DURATION/GATE 的量化格。錄音焦點（TRACK/PATTERN/STEP）決定插入的位置。有 pass-thru 監聽，可以預設「等第一個音」才開始錄。 |
| step | A-1 → CV-A，A-2 → CV-B，AD-1 → GATE（V×20），AD-2 → DURATION（V×20），D-1 插入，D-2 刪除 | 在**編輯游標**的位置遠端插入或刪除 step。D-1 保持高電位期間，參數會跟著輸入電壓變。 |
| alter | 同 step 模式，但不用 D-1/D-2 | 在播放游標即將播到某一步之前，用輸入電壓改寫它的參數。 |

- CV 輸入會先截到 0–8.192V，再量化到該軌電壓表中最接近的那一格。
- GATE 和 DURATION 的換算：`floor(V × 20)`，上限 99。

---

### 5.1 實作（E4）
- **共通**：
  - 只套用有插線的輸入；沒插線的輸入，對應的 step 參數保持原值。**[手冊未寫，自行決定]**
  - ARM 作用在目前選中的軌道。PUNCH 由按鍵切換，或由 PUNCH 的 gate 輸入控制（高電位時算 punch in）；gate 由高轉低時 punch out。
  - RESET、PAUSE、切換錄音模式，都會結束錄音和 pass-thru。
  - 錄音寫入時會增加 Ctrl+Z 的編輯計數，所以錄完之後可以用 Ctrl+Z 復原。
- **alter**：寫入正在播放的序列。每一步在開始播放的那一刻，被輸入的值改寫。如果 loop 只框住一步，則持續改寫，讓電壓的變化立刻反映出來。
- **step**：寫入正在編輯的序列，在 HOLD 模式下就是副本。
  - D-1 上升沿：在各個 armed 軌道的編輯游標之後插入一步。新步以游標所在的步為基礎，再套用輸入的值；D-1 保持高電位期間，新步的參數持續跟著輸入變化。
  - D-2 上升沿：刪除游標所在的那一步。
- **real-time**：
  - 時間以該軌經過除頻或倍頻後的 clock pulse 為單位，加上目前這個 pulse 已經走過的比例（`Transport::pulseTime`）。
  - 開新的一步：
    - AD-1 上升沿（音符開始）。
    - 設定為 `tr` 的 CV-A 或 CV-B，在量化後的索引改變時；而且必須是在音符按住時，或 AD-1 沒插線時（legato）。
  - 一步結束時：
    - DURATION = 距離下一步開始的時間，四捨五入到格線，範圍 1..99。
    - GATE = 按住的時間，四捨五入到它的格線；如果到下一步開始時還按著，就等於 DURATION。
  - CV 值在這一步開始後半個 pulse 取樣。
  - 錄進去的位置依設定畫面選的 focus 而定：
    - TRACK：整軌最後新增一個 pattern。
    - PATTERN：播放中的 pattern 之後新增一個 pattern。
    - STEP：播放中的那一步之後。
  - 一個 pattern 滿 100 步時，會自動接到新的 pattern；總數到 2000 步時，就停止錄音。
  - 預設「等第一個音」才開始錄。
  - **pass-thru**：armed、未 PAUSE，而且已經 punch in，或 punch out 狀態下偵測到音符（排練）。有插線的輸入會原封不動地送到該軌的輸出：A-1 → CV-A，A-2 → CV-B，AD-1 → GATE。錄音中，該軌原本的播放聲音被 pass-thru 取代。
- **設定畫面**：在 real-time 模式下 arm 一軌時，會自動打開。
  - CV-A、CV-B 的 focus 鍵：切換 `tr` / `--`。
  - DURATION、GATE 的 focus 鍵：選中後，右旋鈕調整量化格線。
  - TRACK、PATTERN、STEP 的 focus 鍵：選擇錄進去的位置，對應的 LED 會閃爍。
  - 再按一次 ARM 關閉畫面。畫面沒開時按 ARM，則是解除 arm。
  - 這份設定是四軌共用的，會存在 patch 裡。

## 6. Rotoinversion（INVERT / ROTATE）

| focus | INVERT | INVERT+ROTATE | ROTATE |
|---|---|---|---|
| PATTERN、TRACK、PART | 反轉順序 | 往前移一步 | 往後移一步 |
| GROUP | 反選 | 選取往前移 | 選取往後移 |

PATTERN、TRACK、PART 這三種情況，只影響**右側 focus 中的那個參數**。例如只移動 DURATION，就能改變節奏而不動到音高。

---

## 7. Storage（VCV 的對應方式）

原機的 microSD 卡在 Rack 裡沒有直接對應的東西，下面是各項功能的處理方式。**[未定]** 以下需要你決定：
- snapshot：原機有 127 格（A1..U7，再加 t1..t9 模板），每格保留歷次修訂和描述文字。目前 ER-101 模組有 16 格、存在 patch 裡。
- 使用者電壓表：目前已經存在 Rack 的外掛設定裡。
- MIDI 檔匯入：原機支援 SMF0 和 SMF1，MIDI 第 1–4 聲道對應第 1–4 軌。Rack 可以改用檔案選擇對話框來做。
- CONFIG.INI 裡的進階選項：可以改做成右鍵選單。
- 韌體更新、SD 卡退出：不需要。

---

### 7.1 實作（E5，依你的選擇：擴充 snapshot + MIDI 匯入）
- **snapshot 的格數**：手冊的說法彼此矛盾，有「127 格」、「A1–U7 加上 t1–t9」、資料夾「A1–L9」三種。這裡採用剛好 127 格：
  - 9 個模板 `t1`–`t9`，排在空白格前面。
  - 空白格 `--`。
  - 117 個一般格 `A1`–`q9`：13 個字母各配數字 1–9。字母是 A b C d E F G H J L n P q，跳過 I、O（會和 1、0 混淆），以及七段顯示器顯示不出來的 K、M（手冊也說 K 無法顯示）。
  - 沒接 ER-102 時，一樣只有 `--` 和 1–16 格。1–16 就是 A1–b7 的同一份資料，所以接上 ER-102 之後，之前存的 snapshot 仍然看得到。
  - 格子只有在第一次存入時才配置記憶體。這次配置發生在 audio 執行緒，但只在使用者按下 SAVE 時才會發生。
  - 全部存在 patch 裡。模板格在 JSON 裡用負數的格號表示。
- **沒有做的部分**：revision 紀錄、snapshot 描述畫面（focus 按下 SNAPSHOT）、STORAGE 開關和 I/O、error 燈（error 燈只在 ER-102 沒接上 ER-101 時亮）、CONFIG.INI。
- **MIDI 匯入**：右鍵選單的「Import MIDI file...」，有沒有接 ER-102 都可以用（`src/core/Midi.hpp`）。
  - 支援 SMF 0 與 1；不支援 SMF 2 和 SMPTE 時基，會顯示錯誤訊息。
  - MIDI 第 1–4 聲道依序對應第 1–4 軌。匯入後會**取代整份序列**，包括電壓表和選項。可以用 Ctrl+Z 復原。
  - 每個聲道當成單音處理：新的音符開始時，前一個音符就結束；同一時間開始的音符，只保留最後一個。
  - 時間換算：每四分音符 4 個 pulse（手冊 Quick Start 用的 4 ppqn）。
    - 每個音符變成一步：DURATION 到下一個音符開始為止，GATE 等於音符的長度。
    - 音符之前的空白變成休止步（GATE=0）。
    - 超過 99 個 pulse 時，拆成多步並用滿 gate 連接。
  - 數值換算：
    - CV-A = MIDI 音符編號 − 36，所以 C4 = 24，在 12ET 下是 2V。
    - CV-B = 力度 ÷ 127 × 96（0–8V）。
  - 每 16 步切成一個 pattern。超過 2000 步的部分捨棄，並提示使用者。
  - 原機的「進階方法」（用 INI 指定聲道、移調、量化）沒有做。

## 8. 模組間通訊（VCV）

- 使用 Rack 的 expander 機制：ER-101 模組的 `rightExpander` 必須緊鄰一個 SequencerController。
- 所有 ER-102 的狀態都存在 ER-101 模組的 `Sequence` 裡，包括 parts、groups、group 運算、slope 矩陣、錄音設定。這樣 snapshot、HOLD/COMMIT、Ctrl+Z 都會自動涵蓋它們。
- 訊息的方向：
  - ER-102 → ER-101：按鍵狀態、開關位置、12 個輸入的電壓。
  - ER-101 → ER-102：燈號和顯示內容。
  - 雙緩衝，會有 1 個 sample 的延遲。
- ER-102 單獨擺放、或沒有緊鄰 ER-101 時，面板正常顯示，但不會做任何事。

---

## 9. 開發階段

| 階段 | 內容 |
|---|---|
| E1 | 模組外殼：14HP 面板、expander 連線、完整版 MATH（5 運算 + INVERT）、rotoinversion |
| E2 | Parts：RESET TO、focus/pending/playing、TRANSITION（FIRST/LAST/USER）、SELECT/ACTIVATE、part 的複製與清除 |
| E3 | Groups：選取、Euclidean、group 的破壞性運算、X/Y/Z 調變匯流排（slope、high/low） |
| E4 | Recording：alter → step → real-time（最複雜，放最後） |
| E5 | Storage 對應（依你的決定） |
