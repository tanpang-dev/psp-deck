import CoreGraphics
import Foundation

/// Turns the latest analog stick sample into scrolling (or cursor motion in mouse mode) at 60 Hz.
final class StickDriver {
    private let store: ConfigStore
    private var x = 128, y = 128
    private var timer: Timer?
    private var remX = 0.0, remY = 0.0
    var mouseMode = false

    init(store: ConfigStore) {
        self.store = store
    }

    func update(x: Int, y: Int) {
        self.x = x
        self.y = y
        if timer == nil {
            timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 60, repeats: true) { [weak self] _ in self?.tick() }
        }
    }

    func stop() {
        timer?.invalidate()
        timer = nil
        x = 128
        y = 128
    }

    private func tick() {
        let cfg = store.config.stick
        let dx = Double(x - 128), dy = Double(y - 128)
        let mag = (dx * dx + dy * dy).squareRoot()
        let dead = Double(min(max(cfg.deadzone, 0), 120))
        guard mag > dead else {
            remX = 0
            remY = 0
            return
        }
        let v = min(1, (mag - dead) / (127 - dead))
        let curve = v * v  // fine control near the center, fast at the edge
        let ux = dx / mag, uy = dy / mag

        if mouseMode {
            let speed = 18 * cfg.mouseSpeed * curve
            Mouse.move(dx: take(&remX, ux * speed), dy: take(&remY, uy * speed))
        } else {
            let speed = 24 * cfg.scrollSpeed * curve * (cfg.invertScroll ? -1 : 1)
            let vertical = take(&remY, -uy * speed), horizontal = take(&remX, -ux * speed)
            if vertical != 0 || horizontal != 0 {
                CGEvent(scrollWheelEvent2Source: nil, units: .pixel, wheelCount: 2,
                        wheel1: Int32(vertical), wheel2: Int32(horizontal), wheel3: 0)?.post(tap: .cghidEventTap)
            }
        }
    }

    /// Whole pixels to emit now; the fraction carries over so slow speeds still move.
    private func take(_ rem: inout Double, _ amount: Double) -> Double {
        rem += amount
        let whole = rem.rounded(.towardZero)
        rem -= whole
        return whole
    }
}
