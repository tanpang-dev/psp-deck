import AppKit

/// Running apps for the PSP carousel (DESIGN §4.5, APP message). The whole list is resent when an app
/// launches or quits; which one is in front travels in STATUS, so switching apps costs no icon traffic.
final class AppList {
    static let iconSize = 96
    static let maxApps = 64

    struct Entry {
        let pid: Int32
        let name: String
        let rgba: [UInt8]  // iconSize² × RGBA8888, premultiplied: the PSP's GU_PSM_8888 byte order
    }

    /// Called on the main thread with the full list.
    var onChange: (([Entry]) -> Void)?
    private var pending: DispatchWorkItem?
    private var observers: [NSObjectProtocol] = []

    func start() {
        let center = NSWorkspace.shared.notificationCenter
        for name in [NSWorkspace.didLaunchApplicationNotification, NSWorkspace.didTerminateApplicationNotification] {
            observers.append(center.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
                self?.scheduleRefresh()
            })
        }
    }

    /// Several apps often start or quit together (login, Quit All); send one list for the burst.
    func scheduleRefresh(after delay: TimeInterval = 0.3) {
        pending?.cancel()
        let work = DispatchWorkItem { [weak self] in self?.refresh() }
        pending = work
        DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: work)
    }

    private func refresh() {
        let apps = NSWorkspace.shared.runningApplications
            .filter { $0.activationPolicy == .regular && !$0.isTerminated }
            .prefix(Self.maxApps)
        onChange?(apps.map { app in
            Entry(pid: app.processIdentifier, name: app.localizedName ?? "?", rgba: Self.pixels(of: app.icon))
        })
    }

    static func pixels(of icon: NSImage?) -> [UInt8] {
        let n = iconSize
        var data = [UInt8](repeating: 0, count: n * n * 4)
        guard let icon else { return data }
        data.withUnsafeMutableBytes { buf in
            guard let ctx = CGContext(data: buf.baseAddress, width: n, height: n, bitsPerComponent: 8,
                                      bytesPerRow: n * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return }
            ctx.interpolationQuality = .high
            NSGraphicsContext.saveGraphicsState()
            NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: false)
            icon.draw(in: NSRect(x: 0, y: 0, width: n, height: n))
            NSGraphicsContext.restoreGraphicsState()
        }
        return data  // CGContext rows start at the top, which is what the PSP texture expects
    }

    /// Brings an app to the front. Opening the bundle is used because plain activate() from a background
    /// (LSUIElement) app is not honored on recent macOS.
    static func activate(pid: Int32) {
        guard let app = NSRunningApplication(processIdentifier: pid) else { return }
        if let url = app.bundleURL {
            let config = NSWorkspace.OpenConfiguration()
            config.activates = true
            NSWorkspace.shared.openApplication(at: url, configuration: config) { _, error in
                if error != nil {
                    DispatchQueue.main.async { app.activate() }
                }
            }
        } else {
            app.activate()
        }
    }
}
