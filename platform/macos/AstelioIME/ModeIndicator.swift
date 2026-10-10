import Cocoa

/// Shows あ or A near the caret for a moment after the mode changes.
final class ModeIndicator {
    private let panel: NSPanel
    private let label = NSTextField(labelWithString: "あ")
    private var hideWork: DispatchWorkItem?

    init() {
        panel = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 32, height: 32),
                        styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: true)
        panel.level = NSWindow.Level(rawValue: Int(CGWindowLevelForKey(.popUpMenuWindow)))
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = true
        panel.ignoresMouseEvents = true
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .ignoresCycle]

        let effect = NSVisualEffectView(frame: NSRect(x: 0, y: 0, width: 32, height: 32))
        effect.material = .popover
        effect.state = .active
        effect.blendingMode = .behindWindow
        effect.wantsLayer = true
        effect.layer?.cornerRadius = 8
        effect.layer?.masksToBounds = true

        label.font = .systemFont(ofSize: 17, weight: .medium)
        label.alignment = .center
        label.frame = NSRect(x: 0, y: 5, width: 32, height: 22)
        effect.addSubview(label)
        panel.contentView = effect
    }

    func show(japanese: Bool, near caret: NSRect) {
        label.stringValue = japanese ? "あ" : "A"
        var origin = NSPoint(x: caret.minX, y: caret.minY - 36)
        if caret == .zero, let screen = NSScreen.main {
            origin = NSPoint(x: screen.visibleFrame.midX - 16, y: screen.visibleFrame.midY - 16)
        }
        panel.setFrameOrigin(origin)
        panel.orderFrontRegardless()
        hideWork?.cancel()
        let work = DispatchWorkItem { [weak self] in
            self?.panel.orderOut(nil)
        }
        hideWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.8, execute: work)
    }
}
