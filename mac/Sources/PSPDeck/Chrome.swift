import AppKit

/// Chrome's tabs via AppleScript (DESIGN §5, TABS / TAB_SELECT). Needs the Automation permission for
/// Google Chrome, which macOS asks for on first use. Main thread.
enum Chrome {
    static let bundleID = "com.google.Chrome"

    struct Tab {
        let windowID: UInt32
        let index: UInt16  // 1-based, as AppleScript counts
        let active: Bool
        let title: String
    }

    static var isRunning: Bool {
        !NSRunningApplication.runningApplications(withBundleIdentifier: bundleID).isEmpty
    }

    /// All tabs of all windows, frontmost window first. Empty when Chrome is not running (checked first,
    /// because talking to it would launch it). Throws with AppleScript's message on failure.
    static func tabs() throws -> [Tab] {
        guard isRunning else { return [] }
        let source = """
        tell application id "\(bundleID)"
            set out to ""
            repeat with w in windows
                set wid to id of w
                set ai to active tab index of w
                set i to 0
                repeat with t in tabs of w
                    set i to i + 1
                    set out to out & wid & tab & i & tab & (i = ai) & tab & (title of t) & linefeed
                end repeat
            end repeat
            return out
        end tell
        """
        let text = try run(source)
        return text.split(separator: "\n").compactMap { line in
            let f = line.split(separator: "\t", maxSplits: 3, omittingEmptySubsequences: false)
            guard f.count == 4, let wid = UInt32(f[0]), let idx = UInt16(f[1]) else { return nil }
            return Tab(windowID: wid, index: idx, active: f[2] == "true", title: String(f[3]))
        }
    }

    static func select(windowID: UInt32, index: UInt16) throws {
        guard isRunning else { return }
        _ = try run("""
        tell application id "\(bundleID)"
            set w to window id \(windowID)
            set active tab index of w to \(index)
            set index of w to 1
            activate
        end tell
        """)
    }

    private static func run(_ source: String) throws -> String {
        var error: NSDictionary?
        let result = NSAppleScript(source: source)?.executeAndReturnError(&error)
        if let error {
            throw NSError(domain: "Chrome", code: error[NSAppleScript.errorNumber] as? Int ?? -1,
                          userInfo: [NSLocalizedDescriptionKey: error[NSAppleScript.errorMessage] as? String ?? "AppleScript error"])
        }
        return result?.stringValue ?? ""
    }
}
