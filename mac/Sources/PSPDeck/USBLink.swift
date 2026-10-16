import CLibUSB
import DeckProto
import Foundation

/// IF0 of the PSP (vendor bulk pair). A background thread keeps trying to open the device, reads frames
/// while it is open and drops back to searching on any error, so unplugging / restarting either side heals.
final class USBLink {
    enum State: Equatable {
        case searching
        case open(speed: Int32)  // LIBUSB_SPEED_*: 2 = full, 3 = high
    }

    /// Called on the reader thread.
    var onFrame: ((UInt8, [UInt8]) -> Void)?
    /// Called on the reader thread.
    var onState: ((State) -> Void)?

    private let lock = NSLock()
    private var handle: OpaquePointer?
    private let writer = DispatchQueue(label: "pspdeck.usb.write")

    func start() {
        let t = Thread { [weak self] in self?.run() }
        t.name = "pspdeck.usb.read"
        t.start()
    }

    /// Queues one frame; silently dropped while no PSP is connected.
    func send(_ type: Int, _ payload: [UInt8] = []) {
        writer.async { [weak self] in
            guard let self else { return }
            var frame = [UInt8](repeating: 0, count: Int(DECK_MAX_FRAME))
            let n = payload.withUnsafeBytes { deck_frame(&frame, UInt8(type), $0.baseAddress, UInt32(payload.count)) }
            guard n > 0 else { return }
            self.lock.lock()
            defer { self.lock.unlock() }
            guard let h = self.handle else { return }
            var done: Int32 = 0
            libusb_bulk_transfer(h, 0x02, &frame, n, &done, 500)
        }
    }

    private func run() {
        guard libusb_init(nil) == 0 else {
            onState?(.searching)
            return
        }
        var buf = [UInt8](repeating: 0, count: Int(DECK_MAX_FRAME))
        while true {
            guard let h = openDevice() else {
                Thread.sleep(forTimeInterval: 1)
                continue
            }
            onState?(.open(speed: libusb_get_device_speed(libusb_get_device(h))))
            readFrames(h, into: &buf)
            lock.lock()
            handle = nil
            libusb_release_interface(h, 0)
            libusb_close(h)
            lock.unlock()
            onState?(.searching)
        }
    }

    private func openDevice() -> OpaquePointer? {
        guard let h = libusb_open_device_with_vid_pid(nil, UInt16(DECK_VID), UInt16(DECK_PID)) else { return nil }
        guard libusb_claim_interface(h, 0) == 0 else {
            libusb_close(h)
            return nil
        }
        lock.lock()
        handle = h
        lock.unlock()
        return h
    }

    private func readFrames(_ h: OpaquePointer, into buf: inout [UInt8]) {
        while true {
            var n: Int32 = 0
            let r = libusb_bulk_transfer(h, 0x81, &buf, Int32(buf.count), &n, 500)
            if r == LIBUSB_ERROR_TIMEOUT.rawValue {
                continue
            }
            guard r == 0 else { return }
            var hdr = deck_hdr()
            var payload: UnsafePointer<UInt8>?
            let len = deck_parse(buf, n, &hdr, &payload)
            guard len >= 0, let payload else { continue }
            onFrame?(hdr.type, Array(UnsafeBufferPointer(start: payload, count: Int(len))))
        }
    }
}
