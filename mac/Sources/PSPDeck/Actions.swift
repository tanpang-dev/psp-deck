import AppKit
import Carbon.HIToolbox

/// A shortcut such as "cmd+shift+4". Key codes are physical (ANSI) positions, so only keys that sit in
/// the same place on JIS and ANSI keyboards are accepted (DESIGN §5.3).
struct KeyCombo: Equatable {
    let keyCode: CGKeyCode
    let flags: CGEventFlags

    static let modifiers: [String: CGEventFlags] = [
        "cmd": .maskCommand, "command": .maskCommand, "shift": .maskShift, "opt": .maskAlternate,
        "option": .maskAlternate, "alt": .maskAlternate, "ctrl": .maskControl, "control": .maskControl,
    ]

    static let keys: [String: Int] = {
        var k: [String: Int] = [
            "a": kVK_ANSI_A, "b": kVK_ANSI_B, "c": kVK_ANSI_C, "d": kVK_ANSI_D, "e": kVK_ANSI_E, "f": kVK_ANSI_F,
            "g": kVK_ANSI_G, "h": kVK_ANSI_H, "i": kVK_ANSI_I, "j": kVK_ANSI_J, "k": kVK_ANSI_K, "l": kVK_ANSI_L,
            "m": kVK_ANSI_M, "n": kVK_ANSI_N, "o": kVK_ANSI_O, "p": kVK_ANSI_P, "q": kVK_ANSI_Q, "r": kVK_ANSI_R,
            "s": kVK_ANSI_S, "t": kVK_ANSI_T, "u": kVK_ANSI_U, "v": kVK_ANSI_V, "w": kVK_ANSI_W, "x": kVK_ANSI_X,
            "y": kVK_ANSI_Y, "z": kVK_ANSI_Z,
            "0": kVK_ANSI_0, "1": kVK_ANSI_1, "2": kVK_ANSI_2, "3": kVK_ANSI_3, "4": kVK_ANSI_4,
            "5": kVK_ANSI_5, "6": kVK_ANSI_6, "7": kVK_ANSI_7, "8": kVK_ANSI_8, "9": kVK_ANSI_9,
            "tab": kVK_Tab, "space": kVK_Space, "return": kVK_Return, "esc": kVK_Escape, "delete": kVK_Delete,
            "left": kVK_LeftArrow, "right": kVK_RightArrow, "up": kVK_UpArrow, "down": kVK_DownArrow,
        ]
        let fkeys = [kVK_F1, kVK_F2, kVK_F3, kVK_F4, kVK_F5, kVK_F6, kVK_F7, kVK_F8, kVK_F9, kVK_F10, kVK_F11, kVK_F12]
        for (i, code) in fkeys.enumerated() {
            k["f\(i + 1)"] = code
        }
        return k
    }()

    init(keyCode: CGKeyCode, flags: CGEventFlags) {
        self.keyCode = keyCode
        self.flags = flags
    }

    init?(_ text: String) {
        let parts = text.lowercased().split(separator: "+").map { $0.trimmingCharacters(in: .whitespaces) }
        guard let last = parts.last, let code = KeyCombo.keys[last] else { return nil }
        var flags = CGEventFlags()
        for name in parts.dropLast() {
            guard let m = KeyCombo.modifiers[name] else { return nil }
            flags.insert(m)
        }
        self.init(keyCode: CGKeyCode(code), flags: flags)
    }

    init(keyCode: Int, mods: [String]) {
        var flags = CGEventFlags()
        for m in mods {
            if let f = KeyCombo.modifiers[m] {
                flags.insert(f)
            }
        }
        self.init(keyCode: CGKeyCode(keyCode), flags: flags)
    }

    func post() {
        let source = CGEventSource(stateID: .hidSystemState)
        for down in [true, false] {
            let event = CGEvent(keyboardEventSource: source, virtualKey: keyCode, keyDown: down)
            event?.flags = flags
            event?.post(tap: .cghidEventTap)
        }
    }
}

/// Executes what a PSP button is bound to. Runs on the main thread.
final class ActionRunner {
    private let store: ConfigStore
    private(set) var mouseMode = false
    private(set) var micMuted = false
    /// Called when mouse mode or mute changes, so the PSP can be told (MODE message).
    var onModeChange: (() -> Void)?
    var onError: ((String) -> Void)?

    init(store: ConfigStore) {
        self.store = store
    }

    func reset() {
        mouseMode = false
        micMuted = false
    }

    func button(mask: UInt32, down: Bool) {
        // In mouse mode CIRCLE / CROSS are the left / right buttons, whatever they are bound to.
        if mouseMode && (mask == PSPButton.circleMask || mask == PSPButton.cross.mask) {
            Mouse.click(left: mask == PSPButton.circleMask, down: down)
            return
        }
        guard down, let button = PSPButton(mask: mask),
              let binding = store.config.buttons[button.rawValue],
              let p = preset(id: binding.preset) else { return }
        run(p.action, binding: binding, button: button)
    }

    func tabStep(_ dir: Int) {
        let bundle = NSWorkspace.shared.frontmostApplication?.bundleIdentifier ?? ""
        guard let keys = store.config.tabStep[bundle] ?? store.config.tabStep["default"] else { return }
        let text = dir > 0 ? keys.next : keys.prev
        guard let combo = KeyCombo(text) else {
            onError?("tabStep のキー指定 \"\(text)\" が読めません")
            return
        }
        combo.post()
    }

    private func run(_ action: PresetAction, binding: ButtonBinding, button: PSPButton) {
        switch action {
        case .none:
            break
        case .keys(let text):
            KeyCombo(text)?.post()
        case .open(let target):
            open(target)
        case .launchApp:
            if let app = binding.app {
                open(app)
            }
        case .recorded:
            if let code = binding.keyCode {
                KeyCombo(keyCode: code, mods: binding.mods ?? []).post()
            }
        case .custom:
            runCustom(store.config.custom[button.rawValue], button: button)
        case .mouseMode:
            mouseMode.toggle()
            onModeChange?()
        case .micMute:
            micMuted.toggle()
            onModeChange?()
        }
    }

    private func runCustom(_ custom: CustomAction?, button: PSPButton) {
        if let keys = custom?.keys {
            guard let combo = KeyCombo(keys) else {
                onError?("\(button.title) のカスタム keys \"\(keys)\" が読めません")
                return
            }
            combo.post()
        } else if let target = custom?.open {
            open(target)
        } else if let command = custom?.shell {
            let p = Process()
            p.executableURL = URL(fileURLWithPath: "/bin/sh")
            p.arguments = ["-c", command]
            p.standardOutput = FileHandle.nullDevice
            p.standardError = FileHandle.nullDevice
            do {
                try p.run()
            } catch {
                onError?("\(button.title) のコマンドを実行できません: \(error.localizedDescription)")
            }
        } else {
            onError?("\(button.title) はカスタムですが、config.json の custom.\(button.rawValue) がありません")
        }
    }

    /// An app path, a bundle id or a URL.
    private func open(_ target: String) {
        let url: URL?
        if target.contains("://") {
            url = URL(string: target)
        } else if target.hasPrefix("/") || target.hasPrefix("~") {
            url = URL(fileURLWithPath: (target as NSString).expandingTildeInPath)
        } else {
            url = NSWorkspace.shared.urlForApplication(withBundleIdentifier: target)
        }
        guard let url else {
            onError?("\"\(target)\" を開けません")
            return
        }
        NSWorkspace.shared.open(url)
    }
}

enum Mouse {
    static private(set) var leftDown = false

    static var location: CGPoint {
        CGEvent(source: nil)?.location ?? .zero
    }

    static func click(left: Bool, down: Bool) {
        let type: CGEventType = left ? (down ? .leftMouseDown : .leftMouseUp) : (down ? .rightMouseDown : .rightMouseUp)
        if left {
            leftDown = down
        }
        CGEvent(mouseEventSource: nil, mouseType: type, mouseCursorPosition: location,
                mouseButton: left ? .left : .right)?.post(tap: .cghidEventTap)
    }

    static func move(dx: Double, dy: Double) {
        var p = location
        p.x += dx
        p.y += dy
        p = clampToDisplays(p)
        let type: CGEventType = leftDown ? .leftMouseDragged : .mouseMoved
        CGEvent(mouseEventSource: nil, mouseType: type, mouseCursorPosition: p, mouseButton: .left)?
            .post(tap: .cghidEventTap)
    }

    private static func clampToDisplays(_ p: CGPoint) -> CGPoint {
        var count: UInt32 = 0
        var ids = [CGDirectDisplayID](repeating: 0, count: 16)
        CGGetActiveDisplayList(16, &ids, &count)
        let bounds = ids.prefix(Int(count)).map { CGDisplayBounds($0) }
        if bounds.contains(where: { $0.contains(p) }) {
            return p
        }
        // Off every screen: pull it back onto the nearest one.
        guard let nearest = bounds.min(by: { distance($0, p) < distance($1, p) }) else { return p }
        return CGPoint(x: min(max(p.x, nearest.minX), nearest.maxX - 1), y: min(max(p.y, nearest.minY), nearest.maxY - 1))
    }

    private static func distance(_ r: CGRect, _ p: CGPoint) -> Double {
        let dx = max(r.minX - p.x, 0, p.x - r.maxX)
        let dy = max(r.minY - p.y, 0, p.y - r.maxY)
        return dx * dx + dy * dy
    }
}
