import AppKit
import ServiceManagement
import SwiftUI

struct SettingsView: View {
    @ObservedObject var store: ConfigStore
    @State private var recording: PSPButton?
    @State private var monitor: Any?
    @State private var loginError: String?

    var body: some View {
        Form {
            Section("ボタン") {
                ForEach(PSPButton.allCases) { button in
                    row(for: button)
                }
            }
            Section("スティック") {
                LabeledContent("デッドゾーン") {
                    Slider(value: stickBinding(\.deadzone), in: 5...80)
                }
                LabeledContent("スクロール速度") {
                    Slider(value: stickBinding(\.scrollSpeed), in: 0.2...3)
                }
                LabeledContent("マウス速度") {
                    Slider(value: stickBinding(\.mouseSpeed), in: 0.2...3)
                }
                Toggle("スクロールの向きを反転", isOn: Binding(
                    get: { store.config.stick.invertScroll },
                    set: { v in store.update { $0.stick.invertScroll = v } }))
            }
            Section("その他") {
                Toggle("ログイン時に起動", isOn: Binding(get: { SMAppService.mainApp.status == .enabled },
                                                     set: setLoginItem))
                if let loginError {
                    Text(loginError).foregroundStyle(.red)
                }
                Text("マウスモード中は ○ が左クリック、× が右クリックになります。")
                    .foregroundStyle(.secondary)
                Text("Mac の録音アプリで入力に「\"PSP\" Type C」を選ぶと、PSP の内蔵マイクが使えます。")
                    .foregroundStyle(.secondary)
                if let error = store.error {
                    Text(error).foregroundStyle(.red)
                }
            }
        }
        .formStyle(.grouped)
        .frame(minWidth: 520, idealWidth: 560, minHeight: 480, idealHeight: 720)
        .onDisappear(perform: stopRecording)
    }

    @ViewBuilder
    private func row(for button: PSPButton) -> some View {
        let binding = store.config.buttons[button.rawValue] ?? ButtonBinding(preset: "none")
        HStack {
            Picker(button.title, selection: Binding(
                get: { binding.preset },
                set: { id in store.update { $0.buttons[button.rawValue] = ButtonBinding(preset: id) } })) {
                ForEach(presets) { Text($0.title).tag($0.id) }
            }
            switch binding.preset {
            case "launchApp":
                Button(binding.app.map { URL(fileURLWithPath: $0).deletingPathExtension().lastPathComponent } ?? "選択…") {
                    chooseApp(for: button)
                }
            case "recorded":
                Button(recording == button ? "キーを押してください…" : (binding.label ?? "記録")) {
                    startRecording(button)
                }
            case "custom":
                Button("JSON を開く") {
                    NSWorkspace.shared.open(ConfigStore.url)
                }
            default:
                EmptyView()
            }
        }
    }

    private func stickBinding<T: BinaryFloatingPoint>(_ key: WritableKeyPath<StickConfig, T>) -> Binding<Double> {
        Binding(get: { Double(store.config.stick[keyPath: key]) },
                set: { v in store.update { $0.stick[keyPath: key] = T(v) } })
    }

    private func stickBinding(_ key: WritableKeyPath<StickConfig, Int>) -> Binding<Double> {
        Binding(get: { Double(store.config.stick[keyPath: key]) },
                set: { v in store.update { $0.stick[keyPath: key] = Int(v) } })
    }

    private func chooseApp(for button: PSPButton) {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.application]
        panel.directoryURL = URL(fileURLWithPath: "/Applications")
        guard panel.runModal() == .OK, let url = panel.url else { return }
        store.update { $0.buttons[button.rawValue] = ButtonBinding(preset: "launchApp", app: url.path) }
    }

    private func startRecording(_ button: PSPButton) {
        stopRecording()
        recording = button
        monitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { event in
            let mods = Self.modifierNames(event.modifierFlags)
            let label = Self.symbols(event.modifierFlags) + Self.keyName(event)
            store.update {
                $0.buttons[button.rawValue] = ButtonBinding(preset: "recorded", keyCode: Int(event.keyCode),
                                                            mods: mods, label: label)
            }
            stopRecording()
            return nil  // swallow the key so the window does not react to it
        }
    }

    private func stopRecording() {
        if let monitor {
            NSEvent.removeMonitor(monitor)
        }
        monitor = nil
        recording = nil
    }

    private func setLoginItem(_ on: Bool) {
        do {
            if on {
                try SMAppService.mainApp.register()
            } else {
                try SMAppService.mainApp.unregister()
            }
            loginError = nil
        } catch {
            loginError = "ログイン項目を変更できませんでした: \(error.localizedDescription)"
        }
    }

    static func modifierNames(_ f: NSEvent.ModifierFlags) -> [String] {
        [(NSEvent.ModifierFlags.control, "ctrl"), (.option, "opt"), (.shift, "shift"), (.command, "cmd")]
            .filter { f.contains($0.0) }.map(\.1)
    }

    static func symbols(_ f: NSEvent.ModifierFlags) -> String {
        [(NSEvent.ModifierFlags.control, "⌃"), (.option, "⌥"), (.shift, "⇧"), (.command, "⌘")]
            .filter { f.contains($0.0) }.map(\.1).joined()
    }

    static func keyName(_ e: NSEvent) -> String {
        let named: [UInt16: String] = [49: "Space", 48: "Tab", 36: "↩", 53: "Esc", 51: "⌫",
                                       123: "←", 124: "→", 125: "↓", 126: "↑"]
        return named[e.keyCode] ?? e.charactersIgnoringModifiers?.uppercased() ?? "#\(e.keyCode)"
    }
}
