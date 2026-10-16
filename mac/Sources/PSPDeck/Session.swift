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
    private var hiSpeed = false
    /// Called on the main thread.
    var onStatus: ((Status) -> Void)?

    init(runner: ActionRunner, stick: StickDriver) {
        self.runner = runner
        self.stick = stick
        runner.onModeChange = { [weak self] in self?.sendMode() }
        link.onState = { [weak self] state in self?.linkChanged(state) }
        link.onFrame = { [weak self] type, payload in
            DispatchQueue.main.async { self?.handle(type: Int(type), payload) }
        }
    }

    func start() {
        link.start()
    }

    private func linkChanged(_ state: USBLink.State) {
        switch state {
        case .searching:
            DispatchQueue.main.async {
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
                sendMode()
                onStatus?(.connected(hiSpeed: hiSpeed))
            } else {
                onStatus?(.versionMismatch(psp: version))
            }
        case Int(DECK_P_BUTTON) where p.count >= 5:
            let mask = UInt32(p[0]) | UInt32(p[1]) << 8 | UInt32(p[2]) << 16 | UInt32(p[3]) << 24
            runner.button(mask: mask, down: p[4] != 0)
        case Int(DECK_P_TAB_STEP) where p.count >= 1:
            runner.tabStep(Int(Int8(bitPattern: p[0])))
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
}
