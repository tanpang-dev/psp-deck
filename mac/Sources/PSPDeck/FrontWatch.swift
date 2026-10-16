import AppKit
import ApplicationServices

/// Polls the frontmost app and its focused window title (the active tab's name in Chrome, Safari,
/// Finder, VS Code…) and reports changes. Main thread.
/// ponytail: 0.3 s polling; switch to AXObserver (kAXTitleChanged / kAXFocusedWindowChanged) if battery matters.
final class FrontWatch {
    struct Front: Equatable {
        var pid: Int32
        var name: String
        var title: String
    }

    var onChange: ((Front) -> Void)?
    private var last: Front?
    private var timer: Timer?

    func start() {
        timer = Timer.scheduledTimer(withTimeInterval: 0.3, repeats: true) { [weak self] _ in self?.poll() }
    }

    /// Forget what was sent so the next poll reports again (after the PSP reconnects).
    func resend() {
        last = nil
        poll()
    }

    private func poll() {
        guard let app = NSWorkspace.shared.frontmostApplication else { return }
        let front = Front(pid: app.processIdentifier, name: app.localizedName ?? "",
                          title: Self.windowTitle(pid: app.processIdentifier) ?? "")
        if front != last {
            last = front
            onChange?(front)
        }
    }

    static func windowTitle(pid: Int32) -> String? {
        let app = AXUIElementCreateApplication(pid)
        AXUIElementSetMessagingTimeout(app, 0.2)  // a hung app must not stall the menu bar
        var window: CFTypeRef?
        guard AXUIElementCopyAttributeValue(app, kAXFocusedWindowAttribute as CFString, &window) == .success,
              let window, CFGetTypeID(window) == AXUIElementGetTypeID() else { return nil }
        var title: CFTypeRef?
        guard AXUIElementCopyAttributeValue(window as! AXUIElement, kAXTitleAttribute as CFString, &title) == .success
        else { return nil }
        return title as? String
    }
}

/// UTF-8 bytes of `text`, cut on a character boundary so it fits in `maxBytes`.
func utf8Prefix(_ text: String, maxBytes: Int) -> [UInt8] {
    var out: [UInt8] = []
    for ch in text {
        let bytes = Array(String(ch).utf8)
        if out.count + bytes.count > maxBytes {
            break
        }
        out += bytes
    }
    return out
}

/// What the PSP's bottom bar shows for a binding.
func pspLabel(for binding: ButtonBinding?) -> String {
    guard let binding, let p = preset(id: binding.preset) else { return "" }
    switch p.action {
    case .launchApp:
        return binding.app.map { URL(fileURLWithPath: $0).deletingPathExtension().lastPathComponent } ?? ""
    case .recorded:
        return binding.label ?? ""
    default:
        return p.label
    }
}
