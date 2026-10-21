import Foundation

/// One PSP button's assignment. `preset` is a `Preset.id`; the other fields belong to specific presets.
struct ButtonBinding: Codable, Equatable {
    var preset: String
    var app: String?          // launchApp: path of the .app
    var keyCode: Int?         // recorded: physical key code
    var mods: [String]?       // recorded: cmd / shift / opt / ctrl
    var label: String?        // recorded: what the settings window shows, e.g. "⌘⇧4"
}

/// Hand-written actions for buttons set to "custom" (exactly one field is used, checked in this order).
struct CustomAction: Codable {
    var keys: String?
    var open: String?
    var shell: String?
}

/// Analog stick tuning. Sticks drift differently per unit, hence the knobs.
struct StickConfig: Codable, Equatable {
    var deadzone = 30
    var scrollSpeed = 1.0
    var mouseSpeed = 1.0
    var invertScroll = false

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        deadzone = try c.decodeIfPresent(Int.self, forKey: .deadzone) ?? deadzone
        scrollSpeed = try c.decodeIfPresent(Double.self, forKey: .scrollSpeed) ?? scrollSpeed
        mouseSpeed = try c.decodeIfPresent(Double.self, forKey: .mouseSpeed) ?? mouseSpeed
        invertScroll = try c.decodeIfPresent(Bool.self, forKey: .invertScroll) ?? invertScroll
    }
}

struct TabStepKeys: Codable {
    var next: String
    var prev: String
}

struct DeckConfig: Codable {
    var buttons: [String: ButtonBinding]
    var custom: [String: CustomAction]
    var stick: StickConfig
    var tabStep: [String: TabStepKeys]

    static let defaults = DeckConfig(
        buttons: [
            "triangle": ButtonBinding(preset: "mouseMode"),
            "square": ButtonBinding(preset: "micMute"),
            "cross": ButtonBinding(preset: "spotlight"),
            "start": ButtonBinding(preset: "screenshotFull"),
            "select": ButtonBinding(preset: "screenshotTool"),
        ],
        custom: [:],
        stick: StickConfig(),
        tabStep: [
            "default": TabStepKeys(next: "ctrl+tab", prev: "ctrl+shift+tab"),
            "com.microsoft.VSCode": TabStepKeys(next: "cmd+opt+right", prev: "cmd+opt+left"),
        ])

    init(buttons: [String: ButtonBinding], custom: [String: CustomAction], stick: StickConfig,
         tabStep: [String: TabStepKeys]) {
        self.buttons = buttons
        self.custom = custom
        self.stick = stick
        self.tabStep = tabStep
    }

    /// Missing sections fall back to the defaults so a partial hand-written file still loads.
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        let d = DeckConfig.defaults
        buttons = try c.decodeIfPresent([String: ButtonBinding].self, forKey: .buttons) ?? d.buttons
        custom = try c.decodeIfPresent([String: CustomAction].self, forKey: .custom) ?? d.custom
        stick = try c.decodeIfPresent(StickConfig.self, forKey: .stick) ?? d.stick
        tabStep = try c.decodeIfPresent([String: TabStepKeys].self, forKey: .tabStep) ?? d.tabStep
    }
}

/// ~/Library/Application Support/PSPDeck/config.json. The settings window owns `buttons` and `stick`;
/// `custom` and `tabStep` are only ever edited by hand, so saves leave them untouched on disk.
final class ConfigStore: ObservableObject {
    static let url = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent("Library/Application Support/PSPDeck/config.json")

    @Published private(set) var config = DeckConfig.defaults
    @Published private(set) var error: String?

    func load() {
        do {
            if !FileManager.default.fileExists(atPath: Self.url.path) {
                try FileManager.default.createDirectory(at: Self.url.deletingLastPathComponent(),
                                                        withIntermediateDirectories: true)
                try write(Self.encode(DeckConfig.defaults))
            }
            config = try JSONDecoder().decode(DeckConfig.self, from: Data(contentsOf: Self.url))
            error = nil
        } catch {
            // Keep running on the previous settings; the message shows in the menu and settings window.
            self.error = "設定ファイルを読めませんでした: \(error.localizedDescription)"
        }
    }

    func update(_ change: (inout DeckConfig) -> Void) {
        var next = config
        change(&next)
        config = next
        do {
            var raw = (try? JSONSerialization.jsonObject(with: Data(contentsOf: Self.url))) as? [String: Any] ?? [:]
            let mine = try JSONSerialization.jsonObject(with: JSONEncoder().encode(next)) as? [String: Any] ?? [:]
            raw["buttons"] = mine["buttons"]
            raw["stick"] = mine["stick"]
            try write(JSONSerialization.data(withJSONObject: raw, options: [.prettyPrinted, .sortedKeys]))
            error = nil
        } catch {
            self.error = "設定を保存できませんでした: \(error.localizedDescription)"
        }
    }

    static func encode(_ config: DeckConfig) throws -> Data {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        return try encoder.encode(config)
    }

    private func write(_ data: Data) throws {
        try data.write(to: Self.url, options: .atomic)
    }
}
