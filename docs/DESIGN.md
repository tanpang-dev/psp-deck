# PSP Deck 設計書

PSP-3000 を USB で Mac につなぎ、PSP の画面とボタンで Mac を操作する Stream Deck 風コントローラー。

## 0. 前提環境（確認済み）

| 項目 | 値 |
|---|---|
| PSP | PSP-3000 / 6.61 ARK-5（常駐）/ 自作ソフトは EBOOT として起動 |
| Mac | macOS 27.0.1 / Apple Silicon / Swift 6.4 / libusb 1.0.30（Homebrew で導入済み） |
| 署名 | 手元の Apple Development 証明書を使う（`build-app.sh` に環境変数 `SIGN_ID` で渡す）。ad-hoc 署名だと、再ビルドのたびにアクセシビリティ権限が外れるため |
| 対象ブラウザ | Google Chrome |

## 1. 主な判断

| 論点 | 決定 | 理由 |
|---|---|---|
| PSP 側の構成 | **カーネル PRX（USB ドライバ）＋ユーザー EBOOT（UI）** | `sceUsbbdRegister` は `sceUsbBus_driver`（カーネル専用ライブラリ）にある。6.xx では EBOOT をカーネルモードで動かせないので、PRX を分け、syscall エクスポート（属性 0x4001）で EBOOT から呼べるようにする。PSPLink の usbhostfs と同じ構造 |
| USB デバイス | VID `0x054C` / PID `0x01CA` の**複合デバイス**。IF0 はベンダー固有（bulk IN/OUT）で制御用、IF1/IF2 は USB Audio Class 2.0 のマイク | 制御とマイクを 1 本のケーブルで同時に使える。PID は PSPLink（0x01C9）とぶつからない値にする |
| マイク | **案 A: PSP を UAC2 のマイクとして見せる**（hi-speed、125μs ごとの iso IN）。macOS の標準入力デバイスとしてそのまま使える | Mac 側はコード不要で、仮想オーディオデバイスも要らない。UAC1 では成り立たないことをフェーズ 0 で確かめた（§7 の結果） |
| Mac 側の USB | libusb（SwiftPM の systemLibrary）で IF0 だけを claim する | ベンダー固有のインターフェースはどのドライバも掴まない。オーディオのインターフェースは macOS 標準のドライバに任せる |
| 現在のタブ名 | **前面ウィンドウのタイトル**（Accessibility API の `kAXFocusedWindow` → `kAXTitle`） | Chrome・Safari・Finder・VS Code のどれでも、アクティブなタブの名前になる。AppleScript もアプリごとの分岐も要らない |
| タブの前後移動 | 既定は `ctrl+tab` / `ctrl+shift+tab`、アプリごとに上書きできる（VS Code は `cmd+opt+right/left`） | `cmd+shift+]` は記号キーで、JIS 配列だと別の文字になる（§5.3） |
| Chrome のタブ一覧 | AppleScript（`NSAppleScript`） | タブ一覧とジャンプはウィンドウタイトルからは取れない |
| 日本語表示 | intraFont で `flash0:/font/jpn0.pgf` を読み、UTF-8 で描く | PSP 本体のフォントなので配布物に含めずに済む。jpn0.pgf はどの地域の本体にも入っている |
| ボタン割り当て | **設定画面でプリセットから選ぶ**。「カスタム」を選んだボタンだけ JSON の定義を使う | §5.2 |
| アプリ一覧の見せ方 | **疑似 3D のカルーセル**。十字キーの左右で回し、○ で前面に出す | 2D のスプライトを楕円上に並べ、奥行きに応じて大きさと透明度を変えて、奥から順に描く。GU の 3D 行列は使わないので軽く、コードも短い（§4.5） |
| アナログスティック | 通常はスクロール。プリセット「マウスモード切替」でカーソル移動に切り替え、そのときは ○＝左クリック、×＝右クリック | デッドゾーンと速度は実機に合わせて調整できるよう設定値にする（スティックは個体ごとにずれがあるため） |

## 2. 全体構成

```
┌──────────── PSP-3000 ─────────────┐           ┌──────────── Mac ───────────────────────┐
│ EBOOT (user)                       │           │ PSPDeck.app（メニューバーに常駐）       │
│  main loop 60fps: 入力・UI 描画    │           │  USBLink    : libusb で IF0 を読み書き │
│  rx thread : deckUsbRead→状態更新  │  IF0 bulk │  Session    : HELLO・メッセージ振り分け│
│        │ syscall                   │◄─────────►│  Actions    : CGEvent / open / shell   │
│ deck.prx (kernel)                  │           │  FrontWatch : 前面アプリとタイトル(AX) │
│  sceUsbbd（複合記述子）            │           │  AppList    : NSWorkspace＋アイコン    │
│  mic thread: sceAudioInput         │  IF2 iso  │  Chrome     : AppleScript (P4)         │
│    → iso IN（alt1 の間だけ動く）   │──────────►│  Settings   : SwiftUI の設定画面       │
└────────────────────────────────────┘           │ macOS 標準 USB Audio → 入力「PSP」     │
                                                 └────────────────────────────────────────┘
```

マイクの処理は PRX の中で完結させる。Mac の録音アプリが入力を開くとホストが IF2 を alt1 に切り替えるので、その間だけマイクのスレッドを動かす。EBOOT は PRX から「マイク使用中」のフラグを受け取って画面に出すだけ。

## 3. ディレクトリ構成

```
psp-deck/
├── docs/DESIGN.md
├── proto/
│   ├── deck_proto.h        # メッセージ型・定数・レイアウトの唯一の定義（PSP と Mac で共有）
│   └── test_proto.c        # フレームの組み立て・解析とパディング規則の自己チェック
├── psp/
│   ├── Makefile            # EBOOT.PBP（prx/ も一緒にビルドする）
│   ├── main.c              # 起動、PRX のロード、メインループ
│   ├── link.c              # 受信スレッド、送信、受信した状態
│   ├── ui.c                # GU と intraFont（P2〜）
│   └── prx/
│       ├── Makefile        # deck.prx
│       ├── driver.c        # 記述子、sceUsbbd のコールバック、bulk の読み書き
│       ├── mic.c           # UAC：alt 設定の切り替え、sceAudioInput → iso
│       └── exports.exp
├── mac/
│   ├── Package.swift
│   ├── Sources/CLibUSB/    # module.modulemap（pkg-config libusb-1.0）
│   ├── Sources/DeckProto/  # proto/deck_proto.h を Swift から import するための C ターゲット
│   ├── Sources/PSPDeck/    # main, USBLink, Session, Actions, Presets, Settings(View), FrontWatch, AppList, Chrome
│   ├── build-app.sh        # .app にまとめ、Info.plist を入れて codesign する
│   └── tools/usbecho.c     # S1 用のエコー・速度測定ツール
└── spike/usb-echo/        # S1 用の EBOOT。受けたフレームをそのまま返し、診断情報を表示する。
                            # 起動時のメニューで USB の構成（UAC2 / ベンダー iso の診断 / バルクのみ）を選ぶ
```

## 4. PSP 側

### 4.1 USB 記述子（hi-speed と full-speed の 2 組）

| IF | クラス | alt | エンドポイント |
|---|---|---|---|
| 0 | 0xFF ベンダー固有 | 0 | 0x81 bulk IN、0x02 bulk OUT（HS は 512B、FS は 64B） |
| — | IAD（IF1〜IF2 をまとめる。IF0 の bulk OUT の extra に載せて IF1 の直前に出す） | — | — |
| 1 | 0x01/0x01/0x20 AudioControl v2 | 0 | なし。Header v2（マイク）、Clock Source（ID 0x10、内部固定、周波数は読み取りのみ）、Input Terminal（0x0201 Microphone、1ch）、Output Terminal（0x0101 USB Streaming） |
| 2 | 0x01/0x02/0x20 AudioStreaming v2 | 0 | なし（帯域なし） |
| 2 | 同上 | 1 | 0x83 iso IN async、**bInterval 1**（HS は 125μs、FS は 1ms）、wMaxPacketSize は HS で 20 バイト（10 サンプル）。AS General v2（PCM、1ch）、Type I Format v2（2 バイト、16bit） |

- デバイス記述子は IAD を使うので Misc / Common / IAD（0xEF/0x02/0x01）にする
- 記述子の配列（エンドポイントとインターフェース）は**末尾に 0 の要素を置く**。置かないと `sceUsbStart` が INVALID_ARGUMENT（0x80243002）を返す
- コンフィグ記述子の `bmAttributes` と `bMaxPower` は、PSP のバスドライバが c0 / 01 に書き換える。それでも接続中は充電される
- Feature Unit（ミュートや音量）は置かない。音量は PSP 側の gain で決める。ホストからのクラスリクエストはクロックの GET_CUR / GET_RANGE（周波数）と GET_CUR（有効）だけで、EP0 で応答する
- レートは 44.1kHz 固定（M1 で `sceAudioInput` の音質を確認して変えるかを決める）
- `make -C psp/prx check` で、記述子をバスドライバと同じ順序でバイト列にし、UAC2 の構造を Mac 上で事前に検査できる。macOS は壊れた記述子のエンドポイントを開くとカーネルパニックすることがあるので、記述子を変えたら必ず通す

### 4.2 deck.prx（カーネル）

usbhostfs の `main.c` から次を流用する: 記述子の組み方、`UsbDriver`、`set_bulkin_req` / `set_bulkout_req`、イベントフラグで完了を待つ方式。複合デバイスの扱いと alt 設定の切り替え（`UsbDriver` の 2 番目のコールバックが `changeSetting(interface, alt)`）は、xerpi/psp-uvc-usb-video-class を参考にする。こちらはライセンスが明記されていないので、コードはコピーせず、挙動を参照するだけにする。

- 送信と受信でバッファを分ける（64 バイト境界に揃えた各 64KB）。送信は `Sema` で直列化する
- **送信にタイムアウトを付ける**: 500ms で `sceUsbbdReqCancel` して取り消す。Mac アプリが落ちても PSP の UI を固めないため
- EBOOT から受け取るポインタは、ユーザー空間のアドレスか（`addr & 0x80000000 == 0`）と長さを確かめてからコピーする
- **マイク**: `changeSetting(2, 1)` でマイクのスレッドを起動し、`changeSetting(2, 0)`、detach、stop のいずれかで止める。iso の送信要求は**同時に 1 本だけ**出し、完了したら次を出す（2 本出すと 2 本目が詰まる）。1 パケットのサンプル数は経過時間から決め、取りこぼしたマイクロフレームの分は後続のパケットで取り返す。P5 では、`sceAudioInputBlocking` で取り込んだ音をリングバッファに入れ、そこから「たまった分だけ」詰める形に置き換える
- **バスドライバの癖**（フェーズ 0 で判明）:
  - hi-speed で bInterval 4（1ms）の iso は、要求を「完了」と報告しながら長さ 0 のパケットしか出さない。bInterval 1 なら正しく出る
  - ホストがそのマイクロフレームをポーリングしなかった iso パケットは、完了扱いのまま捨てられる
  - `sceUsbbdReqCancelAll` で取り消した要求には onComplete が呼ばれない
  - EP0 の応答が届かずキューに残ることがあるので、次の応答を出す前に `sceUsbbdReqCancel` で取り除く（これをしないと、しばらくして PSP が落ちる）
  - ホストがポーリングをやめても iso の要求を取り消して出し直すことを繰り返さない。取り消すのは alt 0 になったときと終了時だけ

エクスポートする API（`DeckUsb`、属性 0x4001）:

```c
int deckUsbStart(void);                 // sceUsbStart(BUS)→sceUsbStart(DECK)→sceUsbActivate(0x1CA)
int deckUsbStop(void);
int deckUsbState(void);                 // bit0-1: 0=未接続 1=full 2=hi / bit8: マイク使用中
int deckUsbRead(void *buf, int max, int timeout_ms);   // 1 フレーム分の bulk OUT
int deckUsbWrite(const void *buf, int len);            // 1 フレーム分の bulk IN、500ms でタイムアウト
int deckMicSetMute(int mute);                          // ミュート中は iso に無音を流す（デバイスは消さない）
int deckSetBacklight(int on);                          // 無操作のとき画面を暗くする（sceDisplaySetBrightness。6.xx の NID は実装時に確認）
```

- **充電**: attach のときに `sceSysconPermitChargeBattery()` と `scePowerBatteryPermitCharging()` を呼ぶ。効いたかどうかは `scePowerIsBatteryCharging()` で確かめる（S1-g）。上部バーに充電中の印を出す

### 4.3 EBOOT（ユーザー）

- 起動時に `sceKernelLoadModule("deck.prx")`、`sceKernelStartModule`、`deckUsbStart()` を順に呼ぶ。終了時は逆の順で止める
- **受信スレッド**: `deckUsbRead` でフレームを受け取り、`link.c` の状態（接続、前面アプリ、タイトル、アプリ一覧、タブ一覧、割り当てのラベル）を更新する。メインスレッドとの排他はミューテックス 1 つで行う
- **メインループ**: `sceCtrlReadBufferPositive` で入力を読み、§4.4 の規則に従って送信する。描画は P1 では `pspDebugScreen`（ASCII のみ）、P2 以降は GU と intraFont

### 4.4 ボタンの扱い

PSP 側で意味を解釈するのは UI に関わるボタンだけで、それ以外は生のイベントとして送る。割り当ては Mac 側で決める。

| ボタン | 画面 | PSP の動作 |
|---|---|---|
| L / R | 全画面 | **離したとき**に `TAB_STEP(-1/+1)` を送る。押している間にもう片方が押されたら同時押しとして扱い、単独の動作は出さない |
| L+R 同時押し | ホーム | `TABLIST_REQ` を送り、タブ一覧の画面を開く |
| 十字キー左右 / ○ | ホーム（P3〜） | カルーセルを回し、○ で `APP_ACTIVATE(pid)` |
| 十字キー / ○ / × | タブ一覧 | 上下で選び、○ で `TAB_SELECT`、× で閉じる |
| △ □ × START SELECT | ホーム | `BUTTON(mask, down/up)` を送る |
| ○ / × | マウスモード中 | `BUTTON` を送る（Mac 側で左クリック、右クリックにする） |
| アナログスティック | 全画面 | 生の値（0〜255）が前回送った値と変わったときに `STICK` を送る（最大 60Hz） |
| どれか | 画面が暗いとき | 画面を明るくするだけで、そのボタンの動作はしない。60 秒操作がないと暗くなる |

L/R を離したときに発火させるのは、同時押しと区別するのにタイマーが要らず、いちばん単純だから。代わりに、押してから離すまでの時間（50〜100ms）だけ反応が遅れる。

### 4.5 画面（P2 以降）

```
┌ 480×272 ─────────────────────────────────────────┐
│ Google Chrome ─ 新しいタブ - Google 検索 ⚡ ●MIC │ ← 上部バー 20px。⚡ は充電中、●MIC / ○MUTE はマイクを使っている間だけ
├───────────────────────────────────────────────────┤
│                                                   │
│      ▫     ◻      ┏━━━━┓      ◻     ▫            │ ← カルーセル。中央は 96px、両脇ほど小さく薄く
│                   ┃ ■  ┃                          │
│                   ┗━━━━┛                          │
│                Google Chrome                      │ ← 中央のアプリ名
├───────────────────────────────────────────────────┤
│ △Spotlight □ミュート ×— START全体 SELECTツール │ ← 割り当てのラベル（LABELS）
└───────────────────────────────────────────────────┘
```

**カルーセル**: アプリ i の角度は θ = (i − pos) × 30°。x = 240 + 170·sinθ、大きさ = 0.5 + 0.5·cosθ、透明度 = 0.3 + 0.7·cosθ で、|θ| > 100° のものは描かない。cosθ の小さい順（奥から）に描く。pos は毎フレーム目標値へ 25% ずつ近づけてアニメーションさせる。2 秒以上操作がなければ、前面のアプリ（STATUS の `front_pid`）を中央に戻す。

**滑らかさの要件**: どんな操作をしても 60fps を保ち、カクつかせない。
- 描画はダブルバッファにし、毎フレーム `sceGuSync` と `sceDisplayWaitVblankStart` で垂直同期をとる。1 フレームの処理は 16.6ms 以内に収める（目安は 8ms 以下。デバッグビルドでは処理時間を画面の隅に出す）
- 位置は float で持ち、頂点も float 座標で渡す（GU は 1 ピクセル未満の位置も描ける）。テクスチャは `GU_LINEAR` で補間するので、拡大縮小しても段差が出ない
- アニメーションには臨界減衰のバネ（ω≈18/s）を使う。目標を連続で変えても速度が引き継がれるので、十字キーを押しっぱなしにしてリピートしても動きが途切れない。リピートは 0.35 秒待ってから 0.12 秒ごと
- 受信スレッドはアイコンを作業用のバッファに展開し終えてから、ポインタを差し替えるだけにする。描画中のテクスチャを直接書き換えないので、APP を受信している間もフレームが落ちず、絵も崩れない
- 実装のあとで、20 個のアプリを高速に回してフレーム落ちが 0 であることを確かめる（P3 の完了条件）

**アイコン**は 96×96 で受け取り、128×128 のテクスチャの左上に置いて、UV 0〜96 で描く（GU のテクスチャは 2 のべき乗の大きさにする必要があるため）。

タブ一覧は全画面のリストにする（1 行 16px で約 15 行、スクロールできる）。jpn0.pgf に無い文字（絵文字など）は表示されないが、それは許容する。

## 5. Mac 側

- **形態**: SwiftPM の実行ファイルを `build-app.sh` で `PSPDeck.app` にまとめる。Info.plist には `LSUIElement=1` と `NSAppleEventsUsageDescription` を入れ、Apple Development 証明書で署名する
- **メニュー**: 接続状態（未接続 / full / hi）、`設定…`、`JSON を開く`、`再読み込み`、`終了`
- **USBLink**: 未接続の間は 1 秒ごとに `libusb_open_device_with_vid_pid(0x054C, 0x01CA)` を試す。開けたら IF0 だけを claim し、HELLO を送って受信スレッドを動かす。エラーやデバイスの取り外しを検出したら、未接続の状態に戻る
  - オーディオのインターフェースを掴んでいる macOS 標準のドライバから IF0 を claim できるかは、S1-f で確認する
- **FrontWatch**: 0.3 秒ごとに前面アプリの pid と前面ウィンドウのタイトルを取り、変わったときだけ `STATUS` を送る（`ponytail:` ポーリング。電池消費が気になったら AXObserver に置き換える）
- **AppList**: `NSWorkspace.runningApplications` のうち `.regular` のものを対象にする。起動と終了の通知を受けたら一覧をまるごと送り直す。アイコンは 96×96 の RGBA8888（premultiplied）で、GU の `GU_PSM_8888` と同じバイト順。1 個 36KB、20 個で約 740KB（hi-speed なら 0.1 秒程度、full-speed でも 1 秒以内の見込み。S1-d で確かめる）
- **Stick**: 60Hz のタイマーで、最後に受け取った STICK の値を処理する。中心からの距離がデッドゾーン未満なら何もしない。それを超えた分を、通常はピクセル単位のスクロール（`CGEvent` の scrollWheel）、マウスモードではカーソル移動（現在位置に足して `mouseMoved`。ボタンを押している間は `leftMouseDragged`）に変換する。速度は 2 乗のカーブで加速させる
- **自動起動**: 設定画面のチェックボックスで `SMAppService.mainApp.register()` / `unregister()` を呼ぶ。`build-app.sh install` で `~/Applications` に置く
- **Chrome**（P4）: 1 本の AppleScript で、全ウィンドウの `id`、各タブの `title`、`active tab index` を取る。ジャンプするときは、`set active tab index of window id W to I` → そのウィンドウを `index 1` に → activate の順に実行する

### 5.1 アクション

| 種類 | 内容 |
|---|---|
| `keys` | `"cmd+shift+4"` のような文字列を CGEvent で送る。使えるキー名は英字、数字、`tab space return esc delete left right up down f1〜f12`、修飾キーは `cmd shift opt ctrl` |
| `open` | アプリのパス、バンドル ID、URL を `NSWorkspace.open` で開く |
| `shell` | `/bin/sh -c` で非同期に実行する。出力は捨てる |
| `rawKey` | 記録したショートカット。`keyCode`（物理キーのコード）と修飾キーをそのまま送り直すので、JIS 配列の記号キーでも記録したとおりに動く |
| `mouseMode` | マウスモードのオンとオフを切り替え、`MODE` で PSP に知らせる |
| `micMute` | ミュートのオンとオフを切り替え、`MODE` で PSP に知らせる（PSP が無音を流す） |

### 5.2 ボタン割り当ての設定

**設定画面（SwiftUI）**: 割り当てられるボタン（△ □ × START SELECT）ごとに、プリセットを選ぶ Picker を 1 行ずつ並べる。

| プリセット | 中身 |
|---|---|
| なし | — |
| Spotlight | `cmd+space` |
| スクリーンショット（範囲 / 全体 / ツール） | `cmd+shift+4` / `cmd+shift+3` / `cmd+shift+5` |
| コピー / ペースト / 取り消し / やり直し | `cmd+c` / `cmd+v` / `cmd+z` / `cmd+shift+z` |
| 新規タブ / タブを閉じる / 閉じたタブを開く / 再読み込み | `cmd+t` / `cmd+w` / `cmd+shift+t` / `cmd+r` |
| 直前のアプリに戻る | `cmd+tab` |
| 画面をロック | `ctrl+cmd+q` |
| マウスモード切替 | `mouseMode` |
| マイクミュート | `micMute` |
| アプリを起動… | 選ぶと NSOpenPanel が開き、選んだ `.app` を `open` する |
| **ショートカットを記録…** | 「記録」を押したあとに押したキーの組み合わせを `rawKey` として保存する（`NSEvent` のローカルモニタで取る）。行にはそのショートカットを表示する（例: ⌘⇧4） |
| **カスタム** | JSON の `custom.<ボタン>` の定義を使う。その行に「JSON を開く」ボタンを置く |

初期値は △ = Spotlight、□ = マイクミュート、× = マウスモード切替、START = スクリーンショット（全体、`cmd+shift+3`）、SELECT = スクリーンショット（ツール、`cmd+shift+5`）。

設定画面には、ほかに次の項目を置く。
- **スティック**: デッドゾーンとスクロール速度、マウス速度のスライダー、スクロール方向を反転するチェックボックス
- **ログイン時に起動**のチェックボックス

**保存先**は `~/Library/Application Support/PSPDeck/config.json` の 1 つだけ。設定画面は `buttons` と `stick` を書き換え、`custom` と `tabStep` には手を付けない。

```json
{
  "buttons": {
    "triangle": { "preset": "spotlight" },
    "square":   { "preset": "micMute" },
    "cross":    { "preset": "custom" },
    "start":    { "preset": "screenshotFull" },
    "select":   { "preset": "recorded", "keyCode": 23, "mods": ["cmd", "shift"] }
  },
  "custom": {
    "cross": { "shell": "open ~/Downloads" }
  },
  "stick": { "deadzone": 30, "scrollSpeed": 1.0, "mouseSpeed": 1.0, "invertScroll": false },
  "tabStep": {
    "default":              { "next": "ctrl+tab",      "prev": "ctrl+shift+tab" },
    "com.microsoft.VSCode": { "next": "cmd+opt+right", "prev": "cmd+opt+left" }
  }
}
```

- プリセットはコードの中の表に定義する。`preset` の値はその ID なので、表示名を変えても設定ファイルは壊れない
- JSON を手で編集して読み込みに失敗したら、メニューと設定画面にエラーの行と内容を出し、直前の設定を使い続ける

### 5.3 記号キーは扱わない（v1）

CGEvent に渡すキーコードは物理的なキーの位置を表すので、JIS 配列では ANSI と違う文字になる（ANSI の `]` の位置は、JIS では `[`）。プリセットと `keys` のキー名は、配列に依存しないキーだけで組んである。記号キーを含むショートカットは、「ショートカットを記録」（`rawKey`）を使えば、記録したときと同じ物理キーを送るので正しく動く。

### 5.4 必要な権限

| 権限 | 用途 | 必要になる時期 |
|---|---|---|
| アクセシビリティ | CGEvent の送信、ウィンドウタイトルの取得 | P1 |
| オートメーション（Google Chrome） | タブ一覧とジャンプ | P4 |
| マイク（録音アプリ側） | 録音アプリが「PSP」の入力を使う | P5 |

## 6. 通信プロトコル v1（IF0）

### 6.1 トランスポートの規則

- **1 回のバルク転送 = 1 フレーム**。最大 65536 バイト（ヘッダを含む）
- 受信側はいつも 64KB のバッファで 1 回だけ読む。転送はショートパケットで終わるので、**フレームの長さが 64 の倍数になるときは、送信側が末尾に 0x00 を 1 バイト足す**。受信側はヘッダの `len` より後ろを無視する。こうすれば、ゼロ長パケットの扱いが libusb、macOS、PSP で違っても影響を受けない
- バイト順はリトルエンディアン。文字列は UTF-8

### 6.2 フレーム

```
off  size  field
0    2     magic = 0x4B44 ("DK")
2    1     type
3    1     reserved = 0
4    4     len（payload のバイト数）
8    len   payload
```

`magic` が違うフレームや、`len` が受信したバイト数を超えるフレームは捨てる。

| type | 向き | 名前 | payload |
|---|---|---|---|
| 0x81 | Mac→PSP | HELLO | `u16 version, u16 reserved` |
| 0x01 | PSP→Mac | HELLO | `u16 version, u16 usb_speed(1=full,2=hi)` |
| 0x82 | Mac→PSP | STATUS | `u32 front_pid`, `app_name\0`, `title\0` |
| 0x83 | Mac→PSP | APP | `u8 index, u8 count, u8 active, u8 0`, `u32 pid`, `u8 rgba[96*96*4]`, `name\0` |
| 0x84 | Mac→PSP | TABS | `u16 count, u16 0`、続いて count 個の `{u32 window_id, u16 tab_index(1起点), u8 active, u8 title_len, title[title_len]}` |
| 0x85 | Mac→PSP | LABELS | `u8 count, u8[3] 0`、続いて count 個の `{u32 mask, label\0}`。ラベルは短い表示名（PSP 側で幅に収まらなければ省略） |
| 0x86 | Mac→PSP | MODE | `u8 mouse_mode, u8 mic_muted, u8[2] 0` |
| 0x07 | PSP→Mac | STICK | `u8 x, u8 y, u8[2] 0`（生の値。中心は約 128） |
| 0x02 | PSP→Mac | BUTTON | `u32 mask（PSP_CTRL_* のうち 1 ビット）, u8 down, u8[3] 0` |
| 0x03 | PSP→Mac | TAB_STEP | `i8 dir(+1/-1), u8[3] 0` |
| 0x04 | PSP→Mac | APP_ACTIVATE | `u32 pid` |
| 0x05 | PSP→Mac | TABLIST_REQ | なし |
| 0x06 | PSP→Mac | TAB_SELECT | `u32 window_id, u16 tab_index, u16 0` |

- APP は 1 個ずつ別のフレームで送る。PSP は `index==0` を受け取ったら一覧を作り直す。`count==0` は一覧が空であることを表す
- TABS のタイトルは 255 バイトまでに切り詰める（UTF-8 の文字の途中では切らない）。フレームに入りきらない分のタブは送らない。Chrome が起動していなければ `count==0` を返す
- 音声は IF2（UAC）で送るので、このプロトコルには入れない
- アプリのバンドル ID は PSP では使わないので、送らない

### 6.3 接続の手順

```
Mac: デバイスを open → HELLO(0x81, v=1)
PSP: HELLO(0x01, v=1, speed)        ※ PSP からは、Mac の HELLO を受け取るまで何も送らない
Mac: v が一致したら APP×N → STATUS → LABELS → MODE を送り、通常の運用に入る
     一致しなければ、両方の画面に「バージョン違い」と出して止まる
```

Mac が HELLO を送り直したら、PSP は状態を初期化する。PING による死活監視は置かない。切断は USB の detach と libusb のエラーで検出する。

## 7. 技術リスクの検証計画（フェーズ 0）

### S1: 独自 USB ドライバ（最優先）

| 手順 | 内容 | 合格条件 |
|---|---|---|
| S1-a | `spike/usb-echo` で、PRX のロード、Register、Start、Activate の戻り値を画面に出す | すべて 0 以上 |
| S1-b | Mac で `system_profiler SPUSBDataType` | `054c:01ca` が見える。**速度（480 か 12 Mb/s）を記録する** |
| S1-c | `mac/tools/usbecho` で長さ 1, 7, 63, 64, 512, 4096, 65535 のフレームを往復させる | すべて一致する |
| S1-d | 各方向に 4MB 送って速度を測る | 結果を記録する |
| S1-e | PSP が送信している最中に Mac 側のツールを終了させ、もう一度起動する | PSP が 500ms で戻ってきて、再接続できる |
| **S1-f** | `deck.prx`（§4.1 の複合記述子。マイクの代わりに 882Hz の試験音（44.1kHz でちょうど 50 サンプル周期）を iso で送る）。Mac の Audio MIDI 設定と QuickTime の録音で確かめる。`usbecho` が出す config 記述子のダンプで、iso エンドポイントが 9 バイトになっているかも見る | (1) Register が iso エンドポイントを受け付ける、(2) macOS に入力デバイスとして出る（名前も記録する）、(3) 882Hz の音がきれいに録れる、(4) その状態で libusb が IF0 を claim でき、エコーが通る |

| S1-g | 電池が満タンでない状態で接続し、PSP の画面に `scePowerIsBatteryCharging()` の値を出す | 充電中になる（オレンジの LED が点く） |

**S1-f が失敗した場合**: 案 A は iso 転送が前提なので、ここで成り立つかどうかが決まる。駄目だったら、ログ（どの手順で何が返ったか）を持ってあなたに報告し、方針を相談する。代替案は「bulk で送り、Mac 側で仮想オーディオデバイスとして見せる」案 B。勝手に切り替えはしない。S1-f が通らなくても、フェーズ 1〜4 は IF0 だけで進められる（記述子から IF1 と IF2 を外せば元に戻る）。

S1-a で失敗した場合の代替案:
- EBOOT からカーネル PRX をロードできないとき: ARK の `kuKernelLoadModule` を使う。それでも駄目なら `GAME.TXT` に登録するプラグイン方式にする
- Register が失敗するとき: `flash0:/kd/usb.prx` がロードされているかを確認する

### フェーズ 0 の結果（2026-10-17、PSP-3000 / 6.61 ARK-5、macOS 27.0.1）

| 項目 | 結果 |
|---|---|
| S1-a | PRX のロード、Register、Start、Activate はすべて 0 |
| S1-b | **hi-speed（480Mb/s）**で接続。Mac からは `"PSP" Type C`（Sony）として見える |
| S1-c | 境界の長さ 12 通りの往復がすべて一致（パディング規則は有効） |
| S1-d | 片方向 **13.6MB/s**（UAC と同時に動かしても変わらない） |
| S1-e | Mac 側が途中で読むのをやめても PSP は固まらず、次の接続で復帰する |
| S1-f | UAC1 は不可（1ms 周期では PSP が空のパケットしか出さず、125μs 周期は UAC1 では記述できない）。**UAC2・125μs で合格**：sox で CoreAudio から直接録った 10 秒間、試験音の欠けは 0。録音を止めたあとに放置しても PSP は落ちない |
| S1-g | 接続中は `charging=1` |

検証のときの注意: ffmpeg の AVFoundation 入力は、512 サンプル単位で 12 サンプルずつ捨てることがあり、正常な音声でも途切れて聞こえる。音の連続性は `sox -D -t coreaudio` で録って確かめる。

### S2: 日本語フォント（P2 の最初）

intraFont で jpn0.pgf を開き、`INTRAFONT_STRING_UTF8` で「漢字かなカナ ABC 123 ①」を描いて、表示できた文字を記録する。

### S3: マイク（P5 の最初）

| 手順 | 内容 | 合格条件 |
|---|---|---|
| M1 | `sceAudioInputInit(0, gain, 0)` と `sceAudioInputBlocking` で、44100 / 22050 / 11025Hz それぞれ 5 秒を `ms0:` に WAV で保存し、Mac で聴く | 内蔵マイクから音が入る。採用するレートと gain を決める |
| M2 | 試験音をマイク入力に置き換える | sox で 10 分録って途切れない。手を叩いて測った遅延が 100ms 未満 |

## 8. 追加機能（採用済み）

| 機能 | 場所 | フェーズ |
|---|---|---|
| アナログスティックでスクロール／マウス（マウスモード切替） | §4.4、§5、§5.2 | P1 |
| START = 全体のスクリーンショット、SELECT = スクリーンショットのツール（初期値） | §5.2 | P1 |
| ショートカットを記録 | §5.1、§5.2 | P1 |
| ログイン時に自動起動 | §5 | P1 |
| 接続中の充電 | §4.1、§4.2、S1-g | P0 で検証し、P1 で組み込む |
| 割り当てのラベル表示 | §4.5、LABELS | P2 |
| 無操作で画面を暗くする | §4.2、§4.4 | P2 |
| アプリ一覧を 3D カルーセルで表示（左右で切り替え） | §4.5 | P3 |
| マイクミュート | §4.2、§5.1 | P5 |

採用しなかったもの: シフト層、アプリごとの割り当て、Chrome 以外のアプリでの L+R 一覧。

## 9. ビルド手順

### 9.1 pspdev（Mac）

```sh
curl -L -o ~/Downloads/pspdev.tar.gz \
  https://github.com/pspdev/pspdev/releases/latest/download/pspdev-macos-latest-arm64.tar.gz
tar -xzf ~/Downloads/pspdev.tar.gz -C ~        # → ~/pspdev
brew install libmpc                            # cc1 が /opt/homebrew の libmpc・gmp・mpfr・zstd を使う
export PSPDEV=$HOME/pspdev PATH=$HOME/pspdev/bin:$PATH
psp-config --pspsdk-path                       # 動作確認
```

v20261001（gcc 15.2.0）で確認済み。intraFont（`libintrafont.a`）は同梱されている。

### 9.2 PSP

```sh
cd spike/usb-echo && make dist   # フェーズ 0：dist/PSPDECKECHO/ を ms0:/PSP/GAME/ にコピーする
cd psp && make            # フェーズ 1 以降：prx/deck.prx と EBOOT.PBP ができる
# XMB の [USB 接続] でメモリースティックをマウントし、次の場所に置く
#   /Volumes/<MS>/PSP/GAME/PSPDECK/EBOOT.PBP
#   /Volumes/<MS>/PSP/GAME/PSPDECK/deck.prx
```

### 9.3 Mac

```sh
cd proto && clang -Wall test_proto.c -o /tmp/test_proto && /tmp/test_proto
cd mac/tools && clang -O2 usbecho.c $(pkg-config --cflags --libs libusb-1.0) -o usbecho   # フェーズ 0
cd mac && ./build-app.sh  # build/PSPDeck.app（Apple Development で署名）
open build/PSPDeck.app    # 初回はアクセシビリティの許可を求められる
```

## 10. フェーズと完了条件

| P | 内容 | 完了条件 |
|---|---|---|
| 0 | S1-a〜g | §7 の S1 の表をすべて満たす（S1-f が駄目なら相談） |
| 1 | 接続、HELLO、BUTTON、STICK、MODE、Actions、設定画面（プリセット、記録、カスタム、スティック、自動起動）、充電 | 設定画面で選んだ割り当てが PSP のボタンで実行され、スティックでスクロールとマウス操作ができる。ケーブルの抜き差しや Mac アプリの再起動のあとも自動で復帰する。接続中に充電される |
| 2 | GU と intraFont、STATUS、TAB_STEP、LABELS、画面の減光 | 上部バーに日本語のタイトルが、下部に割り当てのラベルが出る。Chrome、Safari、Finder、VS Code で L/R によってタブが移る。60 秒操作しないと画面が暗くなる |
| 3 | APP 一覧（カルーセル）と APP_ACTIVATE | カルーセルが左右で滑らかに回り（20 個を押しっぱなしで回し続けてもフレーム落ちが 0）、○ でそのアプリが前面に来る。アプリの起動と終了が反映される |
| 4 | TABS、TAB_SELECT | L+R で Chrome の全ウィンドウのタブが一覧され、○ でジャンプする |
| 5 | マイク（M1 → M2）、ミュート | Mac の入力デバイス「PSP」で PSP の内蔵マイクの声が録れ、ミュートが効く |

テストとして置くのは、`proto/test_proto.c`（フレームとパディング）と、キー文字列の解析・プリセット表の自己チェックだけ。USB と UI は実機で確認する。

## 11. やらないこと

要件でスコープ外のもの（デスクトップの切り替え、通知、音楽）。加えて、PING による死活監視、アイコンのキャッシュ、記号キーの割り当て、AXObserver、Feature Unit（OS 側からの音量調整）は、必要になるまで作らない。画面を暗くするまでの時間（60 秒）やカルーセルの角度の間隔は、当面は PSP 側の定数にしておく。
