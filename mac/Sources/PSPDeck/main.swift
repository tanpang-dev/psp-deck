import AppKit
import ApplicationServices
import Combine
import DeckProto
import SwiftUI

final class AppDelegate: NSObject, NSApplicationDelegate {
    private let store = ConfigStore()
    private lazy var runner = ActionRunner(store: store)
    private lazy var stick = StickDriver(store: store)
    private lazy var session = Session(runner: runner, stick: stick, store: store)
    private var configWatch: AnyCancellable?
    private var statusItem: NSStatusItem!
    private let statusLine = NSMenuItem(title: "PSP を探しています…", action: nil, keyEquivalent: "")
    private let errorLine = NSMenuItem(title: "", action: nil, keyEquivalent: "")
    private var settingsWindow: NSWindow?

    func applicationDidFinishLaunching(_ note: Notification) {
        store.load()
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        statusItem.button?.image = NSImage(systemSymbolName: "gamecontroller", accessibilityDescription: "PSP Deck")
        buildMenu()
        showError(store.error)
        runner.onError = { [weak self] message in self?.showError(message) }
        session.onStatus = { [weak self] status in self?.show(status) }
        session.start()
        configWatch = store.$config.dropFirst().receive(on: DispatchQueue.main).sink { [weak self] _ in
            self?.session.sendLabels()
            self?.session.sendMode()
        }
        // Sending keys and reading window titles both need Accessibility; ask once up front.
        let prompt = kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String
        AXIsProcessTrustedWithOptions([prompt: true] as CFDictionary)
    }

    private func buildMenu() {
        let menu = NSMenu()
        statusLine.isEnabled = false
        errorLine.isEnabled = false
        errorLine.isHidden = true
        menu.addItem(statusLine)
        menu.addItem(errorLine)
        menu.addItem(.separator())
        menu.addItem(item("設定…", #selector(openSettings), ","))
        menu.addItem(item("設定ファイルを開く", #selector(openConfigFile)))
        menu.addItem(item("設定を再読み込み", #selector(reloadConfig)))
        menu.addItem(.separator())
        menu.addItem(item("PSP Deck を終了", #selector(NSApplication.terminate(_:)), "q"))
        statusItem.menu = menu
    }

    private func item(_ title: String, _ action: Selector, _ key: String = "") -> NSMenuItem {
        let item = NSMenuItem(title: title, action: action, keyEquivalent: key)
        item.target = action == #selector(NSApplication.terminate(_:)) ? NSApp : self
        return item
    }

    private func show(_ status: Session.Status) {
        switch status {
        case .searching:
            statusLine.title = "PSP を探しています…"
        case .handshaking:
            statusLine.title = "PSP と接続中…"
        case .connected(let hi):
            statusLine.title = "PSP に接続（\(hi ? "hi-speed" : "full-speed")）"
        case .versionMismatch(let v):
            statusLine.title = "PSP 側のバージョンが違います（PSP v\(v)）"
        }
        statusItem.button?.appearsDisabled = status != .connected(hiSpeed: true) && status != .connected(hiSpeed: false)
    }

    private func showError(_ message: String?) {
        errorLine.title = message ?? ""
        errorLine.isHidden = message == nil
    }

    @objc private func openSettings() {
        if settingsWindow == nil {
            let window = NSWindow(contentViewController: NSHostingController(rootView: SettingsView(store: store)))
            window.title = "PSP Deck の設定"
            window.isReleasedWhenClosed = false
            settingsWindow = window
        }
        NSApp.activate(ignoringOtherApps: true)
        settingsWindow?.center()
        settingsWindow?.makeKeyAndOrderFront(nil)
    }

    @objc private func openConfigFile() {
        NSWorkspace.shared.open(ConfigStore.url)
    }

    @objc private func reloadConfig() {
        store.load()
        showError(store.error)
    }
}

/// `PSPDeck --selftest`: the parsing and config rules that are easy to break without noticing.
func selfTest() {
    let shot = KeyCombo("cmd+shift+4")
    assert(shot?.flags == [.maskCommand, .maskShift] && shot?.keyCode == 21)
    assert(KeyCombo("ctrl+tab")?.keyCode == 48)
    assert(KeyCombo("cmd+opt+right")?.flags == [.maskCommand, .maskAlternate])
    assert(KeyCombo("cmd+]") == nil, "symbol keys are layout dependent and must be rejected")
    assert(KeyCombo("hyper+a") == nil)
    assert(KeyCombo("F12")?.keyCode == 111)
    assert(Set(presets.map(\.id)).count == presets.count, "preset ids must be unique")
    for b in DeckConfig.defaults.buttons.values {
        assert(preset(id: b.preset) != nil, "default uses unknown preset \(b.preset)")
    }
    for keys in DeckConfig.defaults.tabStep.values {
        assert(KeyCombo(keys.next) != nil && KeyCombo(keys.prev) != nil)
    }
    let partial = try! JSONDecoder().decode(DeckConfig.self, from: Data(#"{"stick":{"deadzone":12}}"#.utf8))
    assert(partial.stick.deadzone == 12 && partial.stick.mouseSpeed == 1 && partial.buttons.count == 5)
    assert(PSPButton(mask: 0x4000) == .cross && PSPButton(mask: PSPButton.circleMask) == nil)
    assert(utf8Prefix("あいう", maxBytes: 7) == Array("あい".utf8), "must cut on a character boundary")
    assert(pspLabel(for: ButtonBinding(preset: "launchApp", app: "/Applications/Safari.app")) == "Safari")
    assert(presets.allSatisfy { $0.label.utf8.count <= 31 }, "labels must fit the LABELS field")
    let px = AppList.pixels(of: NSImage(systemSymbolName: "gamecontroller", accessibilityDescription: nil))
    assert(px.count == AppList.iconSize * AppList.iconSize * 4 && px.contains { $0 != 0 }, "icon must render")
    assert(8 + px.count + 64 < Int(DECK_MAX_FRAME) - Int(DECK_HDR_SIZE) - 1, "an APP frame must fit in one transfer")
    print("selftest: ok")
}

if CommandLine.arguments.contains("--selftest") {
    selfTest()
    exit(0)
}

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.setActivationPolicy(.accessory)
app.run()
