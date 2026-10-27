#if DEBUG
import Cocoa
import SwiftUI

/// Debug builds only: draws the settings window into a PNG without screen recording, for the App Store screenshots.
///
///   AstelioIME --snapshot <tab 0-4, or 10-14 for the wizard's pages> <out.png> [ready]
@MainActor
enum SettingsSnapshot {
    static func run(tab: Int, output: URL, ready: Bool) -> Never {
        let application = SnapshotApplication.shared
        application.setActivationPolicy(.regular)
        if ready {
            SetupModel.shownStatus = .ready
        }
        let wizard = tab >= 10
        let size = wizard ? NSSize(width: 820, height: 600) : NSSize(width: 680, height: 640)
        let window = SnapshotWindow(contentRect: NSRect(origin: .zero, size: size),
                              styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        window.title = "Astelio"
        if wizard {
            let view = SetupWizard(setup: SetupModel(), page: tab - 10) {}
            window.contentViewController = NSHostingController(rootView: view.environment(\.controlActiveState, .key))
        } else {
            window.contentViewController = NSHostingController(rootView: SettingsView(tab: tab).environment(\.controlActiveState, .key))
        }
        window.setContentSize(size)
        window.center()
        application.activate(ignoringOtherApps: true)
        window.makeKeyAndOrderFront(nil)
        RunLoop.main.run(until: Date().addingTimeInterval(2))
        // The frame view: the title bar and the tab bar are drawn there, outside the content view.
        guard let view = window.contentView?.superview,
              let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) else {
            exit(1)
        }
        view.cacheDisplay(in: view.bounds, to: rep)
        try? rep.representation(using: .png, properties: [:])?.write(to: output)
        print("wrote \(output.path) \(rep.pixelsWide)x\(rep.pixelsHigh)")
        exit(0)
    }
}
/// Reports itself as the active app, so controls show their active colors while another app is in front.
private final class SnapshotApplication: NSApplication {
    override var isActive: Bool { true }
}

/// Draws as the key window even when another app is in front, so controls show their active colors.
private final class SnapshotWindow: NSWindow {
    override var isKeyWindow: Bool { true }
    override var isMainWindow: Bool { true }
}
#endif
