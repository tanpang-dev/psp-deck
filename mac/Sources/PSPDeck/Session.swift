import DeckProto
import Foundation

/// Protocol v1 on top of USBLink (DESIGN §6): HELLO handshake, then PSP input → actions.
final class Session {
    enum Status: Equatable {
        case searching
        case handshaking
        case connected(hiSpeed: Bool)
        case versionMismatch(psp: Int)
    }

    private let link = USBLink()
    private let runner: ActionRunner
    private let stick: StickDriver
    private let store: ConfigStore
    private let front = FrontWatch()
    private let apps = AppList()
    private var connected = false
    private var hiSpeed = false
    /// Called on the main thread.
    var onStatus: ((Status) -> Void)?
    var onError: ((String) -> Void)?

    init(runner: ActionRunner, stick: StickDriver, store: ConfigStore) {
        self.runner = runner
        self.stick = stick
        self.store = store
        front.onChange = { [weak self] f in self?.sendStatus(f) }
        apps.onChange = { [weak self] list in self?.sendApps(list) }
        runner.onModeChange = { [weak self] in self?.sendMode() }
        link.onState = { [weak self] state in self?.linkChanged(state) }
        link.onFrame = { [weak self] type, payload in
            DispatchQueue.main.async { self?.handle(type: Int(type), payload) }
        }
    }

    func start() {
        link.start()
        front.start()
        apps.start()
    }

    /// One APP frame per app; the PSP swaps its carousel in once the last one has arrived.
    private func sendApps(_ list: [AppList.Entry]) {
        guard connected else { return }
        if list.isEmpty {
            link.send(Int(DECK_M_APP), [0, 0, 0, 0, 0, 0, 0, 0])
        }
        for (i, app) in list.enumerated() {
            let p = [UInt8(i), UInt8(list.count), 0, 0] + le32(UInt32(bitPattern: app.pid)) + app.rgba
                + utf8Prefix(app.name, maxBytes: 63) + [0]
            link.send(Int(DECK_M_APP), p)
        }
    }

    /// Bottom-bar labels for every assignable button; resent whenever the settings change.
    func sendLabels() {
        guard connected else { return }
        var p: [UInt8] = [UInt8(PSPButton.allCases.count), 0, 0, 0]
        for b in PSPButton.allCases {
            p += le32(b.mask) + utf8Prefix(pspLabel(for: store.config.buttons[b.rawValue]), maxBytes: 31) + [0]
        }
        link.send(Int(DECK_M_LABELS), p)
    }

    /// Chrome's tabs for the PSP's L+R list; as many as fit in one frame (count 0 = Chrome not running).
    private func sendTabs() {
        var tabs: [Chrome.Tab] = []
        do {
            tabs = try Chrome.tabs()
        } catch {
            onError?("Chrome のタブを取得できません（オートメーションの許可を確認してください）: \(error.localizedDescription)")
        }
        var body: [UInt8] = []
        var count = 0
        let limit = Int(DECK_MAX_FRAME) - Int(DECK_HDR_SIZE) - 1 - 4
        for t in tabs {
            let title = utf8Prefix(t.title.replacingOccurrences(of: "\n", with: " "), maxBytes: 255)
            let entry = le32(t.windowID) + [UInt8(t.index & 0xFF), UInt8(t.index >> 8), t.active ? 1 : 0,
                                            UInt8(title.count)] + title
            if body.count + entry.count > limit || count == 0xFFFF {
                break
            }
            body += entry
            count += 1
        }
        link.send(Int(DECK_M_TABS), [UInt8(count & 0xFF), UInt8(count >> 8), 0, 0] + body)
    }

    private func sendStatus(_ f: FrontWatch.Front) {
        guard connected else { return }
        let p = le32(UInt32(bitPattern: f.pid)) + utf8Prefix(f.name, maxBytes: 63) + [0]
            + utf8Prefix(f.title, maxBytes: 255) + [0]
        link.send(Int(DECK_M_STATUS), p)
    }

    private func linkChanged(_ state: USBLink.State) {
        switch state {
        case .searching:
            DispatchQueue.main.async {
                self.connected = false
                self.runner.reset()
                self.stick.stop()
                self.onStatus?(.searching)
            }
        case .open(let speed):
            hiSpeed = speed >= 3
            link.send(Int(DECK_M_HELLO), le16(Int(DECK_PROTO_VERSION)) + [0, 0])
            DispatchQueue.main.async { self.onStatus?(.handshaking) }
        }
    }

    private func handle(type: Int, _ p: [UInt8]) {
        switch type {
        case Int(DECK_P_HELLO) where p.count >= 2:
            let version = Int(p[0]) | Int(p[1]) << 8
            if version == Int(DECK_PROTO_VERSION) {
                runner.reset()
                stick.mouseMode = false
                connected = true
                sendMode()
                sendLabels()
                apps.scheduleRefresh(after: 0)
                front.resend()
                onStatus?(.connected(hiSpeed: hiSpeed))
            } else {
                onStatus?(.versionMismatch(psp: version))
            }
        case Int(DECK_P_BUTTON) where p.count >= 5:
            let mask = UInt32(p[0]) | UInt32(p[1]) << 8 | UInt32(p[2]) << 16 | UInt32(p[3]) << 24
            runner.button(mask: mask, down: p[4] != 0)
        case Int(DECK_P_TAB_STEP) where p.count >= 1:
            runner.tabStep(Int(Int8(bitPattern: p[0])))
        case Int(DECK_P_APP_ACTIVATE) where p.count >= 4:
            AppList.activate(pid: Int32(bitPattern: UInt32(p[0]) | UInt32(p[1]) << 8 | UInt32(p[2]) << 16 | UInt32(p[3]) << 24))
        case Int(DECK_P_TABLIST_REQ):
            sendTabs()
        case Int(DECK_P_TAB_SELECT) where p.count >= 6:
            let wid = UInt32(p[0]) | UInt32(p[1]) << 8 | UInt32(p[2]) << 16 | UInt32(p[3]) << 24
            do {
                try Chrome.select(windowID: wid, index: UInt16(p[4]) | UInt16(p[5]) << 8)
            } catch {
                onError?("Chrome のタブを切り替えられません: \(error.localizedDescription)")
            }
        case Int(DECK_P_STICK) where p.count >= 2:
            stick.update(x: Int(p[0]), y: Int(p[1]))
        default:
            break
        }
    }

    private func sendMode() {
        stick.mouseMode = runner.mouseMode
        link.send(Int(DECK_M_MODE), [runner.mouseMode ? 1 : 0, runner.micMuted ? 1 : 0, 0, 0])
    }

    private func le16(_ v: Int) -> [UInt8] {
        [UInt8(v & 0xFF), UInt8(v >> 8 & 0xFF)]
    }

    private func le32(_ v: UInt32) -> [UInt8] {
        [UInt8(v & 0xFF), UInt8(v >> 8 & 0xFF), UInt8(v >> 16 & 0xFF), UInt8(v >> 24)]
    }
}
