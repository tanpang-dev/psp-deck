import Foundation

enum PresetAction {
    case none
    case keys(String)
    case open(String)   // app path, bundle id or URL
    case launchApp
    case recorded
    case custom
    case mouseMode
    case micMute
}

struct Preset: Identifiable {
    let id: String
    let title: String
    let action: PresetAction
    /// Short text for the PSP's bottom bar (about 6 full-width characters fit).
    let label: String
}

/// Choices offered for every assignable button. Ids are stored in config.json, so never rename one.
/// Only layout-independent keys are used (see DESIGN §5.3).
let presets: [Preset] = [
    Preset(id: "none", title: "なし", action: .none, label: ""),
    Preset(id: "spotlight", title: "Spotlight", action: .keys("cmd+space"), label: "Spotlight"),
    Preset(id: "screenshotArea", title: "スクリーンショット（範囲）", action: .keys("cmd+shift+4"), label: "範囲スクショ"),
    Preset(id: "screenshotFull", title: "スクリーンショット（全体）", action: .keys("cmd+shift+3"), label: "全体スクショ"),
    Preset(id: "screenshotTool", title: "スクリーンショット（ツール）", action: .keys("cmd+shift+5"), label: "スクショ"),
    Preset(id: "copy", title: "コピー", action: .keys("cmd+c"), label: "コピー"),
    Preset(id: "paste", title: "ペースト", action: .keys("cmd+v"), label: "ペースト"),
    Preset(id: "undo", title: "取り消し", action: .keys("cmd+z"), label: "取り消し"),
    Preset(id: "redo", title: "やり直し", action: .keys("cmd+shift+z"), label: "やり直し"),
    Preset(id: "newTab", title: "新規タブ", action: .keys("cmd+t"), label: "新規タブ"),
    Preset(id: "closeTab", title: "タブを閉じる", action: .keys("cmd+w"), label: "タブを閉じる"),
    Preset(id: "reopenTab", title: "閉じたタブを開く", action: .keys("cmd+shift+t"), label: "タブ復元"),
    Preset(id: "reload", title: "再読み込み", action: .keys("cmd+r"), label: "再読み込み"),
    Preset(id: "previousApp", title: "直前のアプリに戻る", action: .keys("cmd+tab"), label: "前のアプリ"),
    Preset(id: "lockScreen", title: "画面をロック", action: .keys("ctrl+cmd+q"), label: "ロック"),
    // The Mission Control key sends a special HID code, not F3, so open the app it launches instead.
    Preset(id: "missionControl", title: "Mission Control（F3）", action: .open("/System/Applications/Mission Control.app"),
           label: "Mission"),
    Preset(id: "mouseMode", title: "マウスモード切替", action: .mouseMode, label: "マウス"),
    Preset(id: "micMute", title: "マイクミュート", action: .micMute, label: "ミュート"),
    Preset(id: "launchApp", title: "アプリを起動…", action: .launchApp, label: ""),
    Preset(id: "recorded", title: "ショートカットを記録…", action: .recorded, label: ""),
    Preset(id: "custom", title: "カスタム（JSON）", action: .custom, label: "カスタム"),
]

func preset(id: String) -> Preset? {
    presets.first { $0.id == id }
}

/// The PSP buttons the user can assign, keyed by the names used in config.json.
enum PSPButton: String, CaseIterable, Identifiable {
    case triangle, square, cross, start, select

    var id: String { rawValue }

    /// PSP_CTRL_* bit as sent in BUTTON messages.
    var mask: UInt32 {
        switch self {
        case .select: return 0x0001
        case .start: return 0x0008
        case .triangle: return 0x1000
        case .cross: return 0x4000
        case .square: return 0x8000
        }
    }

    var title: String {
        switch self {
        case .triangle: return "△"
        case .square: return "□"
        case .cross: return "×"
        case .start: return "START"
        case .select: return "SELECT"
        }
    }

    init?(mask: UInt32) {
        guard let b = PSPButton.allCases.first(where: { $0.mask == mask }) else { return nil }
        self = b
    }

    static let circleMask: UInt32 = 0x2000
}
