import Cocoa

/// The candidate window: frosted glass (NSVisualEffectView) with rounded corners, near the focused segment.
/// It never takes the focus from the app; a click picks a candidate.
final class CandidatePanel {
    enum Pick {
        case item(Int)
        case related(Int)
    }

    private let panel: NSPanel
    private let effect = NSVisualEffectView()
    private let stack = NSStackView()
    private var onPick: ((Pick) -> Void)?

    private static let smallFont = NSFont.systemFont(ofSize: 11)

    init() {
        panel = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 200, height: 100),
                        styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: true)
        panel.level = NSWindow.Level(rawValue: Int(CGWindowLevelForKey(.popUpMenuWindow)))
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = true
        panel.hidesOnDeactivate = false
        panel.becomesKeyOnlyIfNeeded = true
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .ignoresCycle]

        effect.material = .popover
        effect.state = .active
        effect.blendingMode = .behindWindow
        effect.wantsLayer = true
        effect.layer?.cornerRadius = 8
        effect.layer?.masksToBounds = true

        stack.orientation = .vertical
        stack.alignment = .width // every row as wide as the widest, so the selection fills the line
        stack.spacing = 0
        stack.edgeInsets = NSEdgeInsets(top: 4, left: 4, bottom: 4, right: 4)
        stack.translatesAutoresizingMaskIntoConstraints = false
        effect.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: effect.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: effect.trailingAnchor),
            stack.topAnchor.constraint(equalTo: effect.topAnchor),
            stack.bottomAnchor.constraint(equalTo: effect.bottomAnchor),
        ])
        panel.contentView = effect
    }

    func hide() {
        panel.orderOut(nil)
        onPick = nil
    }

    func show(_ view: ASTCandidateView, near caret: NSRect, onPick: @escaping (Pick) -> Void) {
        self.onPick = onPick
        stack.arrangedSubviews.forEach { $0.removeFromSuperview() }
        if view.kind == .emoji {
            fillEmoji(view)
        } else {
            fillCandidates(view)
        }
        effect.layoutSubtreeIfNeeded()
        let size = stack.fittingSize
        place(size: NSSize(width: max(size.width, 120), height: size.height), near: caret)
        panel.orderFrontRegardless()
    }

    // MARK: - Rows

    private func fillCandidates(_ view: ASTCandidateView) {
        for (index, item) in view.items.enumerated() {
            let numbered = view.kind == .candidates
            let mark = index < view.marks.count ? ASTCandidateMark(rawValue: view.marks[index].intValue) : nil
            let row = Row(number: numbered ? "\(index + 1)" : nil, text: item,
                          note: mark == .typo ? "もしかして" : (mark == .learned ? "履歴" : nil),
                          selected: index == view.selected) { [weak self] in
                self?.onPick?(.item(index))
            }
            stack.addArrangedSubview(row)
        }
        if !view.related.isEmpty {
            let line = NSStackView()
            line.orientation = .horizontal
            line.spacing = 2
            for (index, emoji) in view.related.enumerated() {
                line.addArrangedSubview(Cell(text: emoji, selected: index == view.relatedSelected) { [weak self] in
                    self?.onPick?(.related(index))
                })
            }
            stack.addArrangedSubview(line)
        }
        let footer: String
        switch view.kind {
        case .predictions:
            footer = "Tab で選ぶ"
        default:
            footer = "\(view.firstIndex + max(view.selected, 0) + 1) / \(view.total)"
        }
        stack.addArrangedSubview(Self.label(footer, font: Self.smallFont, color: .secondaryLabelColor))
    }

    private func fillEmoji(_ view: ASTCandidateView) {
        let title = view.title.isEmpty ? "絵文字（読みで検索・Tab で分類）" : "検索: \(view.title)"
        stack.addArrangedSubview(Self.label(title, font: Self.smallFont, color: .secondaryLabelColor))
        let columns = max(view.columns, 1)
        var line: NSStackView?
        for (index, emoji) in view.items.enumerated() {
            if index % columns == 0 {
                let next = NSStackView()
                next.orientation = .horizontal
                next.spacing = 2
                stack.addArrangedSubview(next)
                line = next
            }
            line?.addArrangedSubview(Cell(text: emoji, selected: index == view.selected) { [weak self] in
                self?.onPick?(.item(index))
            })
        }
    }

    private func place(size: NSSize, near caret: NSRect) {
        let screen = NSScreen.screens.first { $0.frame.intersects(caret) } ?? NSScreen.main
        let visible = screen?.visibleFrame ?? NSRect(x: 0, y: 0, width: 1440, height: 900)
        var origin = NSPoint(x: caret.minX, y: caret.minY - size.height - 4)
        if origin.y < visible.minY {
            origin.y = caret.maxY + 4 // no room below the line: above it
        }
        origin.x = min(max(origin.x, visible.minX), visible.maxX - size.width)
        panel.setFrame(NSRect(origin: origin, size: size), display: true)
    }

    fileprivate static func label(_ text: String, font: NSFont, color: NSColor) -> NSTextField {
        let label = NSTextField(labelWithString: text)
        label.font = font
        label.textColor = color
        label.lineBreakMode = .byTruncatingTail
        return label
    }
}

/// A clickable view that fills its background when selected.
private class Clickable: NSView {
    private let action: () -> Void
    private let selected: Bool

    init(selected: Bool, action: @escaping () -> Void) {
        self.selected = selected
        self.action = action
        super.init(frame: .zero)
        wantsLayer = true
        layer?.cornerRadius = 5
        translatesAutoresizingMaskIntoConstraints = false
    }

    required init?(coder: NSCoder) {
        fatalError("not used")
    }

    override func updateLayer() {
        layer?.backgroundColor = selected ? NSColor.controlAccentColor.withAlphaComponent(0.85).cgColor : nil
    }

    override var wantsUpdateLayer: Bool { true }

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    override func mouseUp(with event: NSEvent) {
        if bounds.contains(convert(event.locationInWindow, from: nil)) {
            action()
        }
    }
}

/// One candidate: its number, the text and a note (もしかして, 履歴).
private final class Row: Clickable {
    init(number: String?, text: String, note: String?, selected: Bool, action: @escaping () -> Void) {
        super.init(selected: selected, action: action)
        let color: NSColor = selected ? .white : .labelColor
        let line = NSStackView()
        line.orientation = .horizontal
        line.spacing = 8
        line.edgeInsets = NSEdgeInsets(top: 3, left: 6, bottom: 3, right: 8)
        line.translatesAutoresizingMaskIntoConstraints = false
        if let number = number {
            let digit = CandidatePanel.label(number, font: .monospacedDigitSystemFont(ofSize: 12, weight: .regular),
                                             color: selected ? .white : .secondaryLabelColor)
            line.addArrangedSubview(digit)
        }
        line.addArrangedSubview(CandidatePanel.label(text, font: .systemFont(ofSize: 15), color: color))
        if let note = note {
            line.addArrangedSubview(CandidatePanel.label(note, font: .systemFont(ofSize: 10),
                                                         color: selected ? .white : .tertiaryLabelColor))
        }
        addSubview(line)
        NSLayoutConstraint.activate([
            line.leadingAnchor.constraint(equalTo: leadingAnchor),
            line.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor),
            line.topAnchor.constraint(equalTo: topAnchor),
            line.bottomAnchor.constraint(equalTo: bottomAnchor),
        ])
    }

    required init?(coder: NSCoder) {
        fatalError("not used")
    }
}

/// One emoji of the palette or of the related column.
private final class Cell: Clickable {
    init(text: String, selected: Bool, action: @escaping () -> Void) {
        super.init(selected: selected, action: action)
        let label = CandidatePanel.label(text, font: .systemFont(ofSize: 20), color: .labelColor)
        label.alignment = .center
        label.translatesAutoresizingMaskIntoConstraints = false
        addSubview(label)
        NSLayoutConstraint.activate([
            widthAnchor.constraint(equalToConstant: 32),
            heightAnchor.constraint(equalToConstant: 30),
            label.centerXAnchor.constraint(equalTo: centerXAnchor),
            label.centerYAnchor.constraint(equalTo: centerYAnchor),
        ])
    }

    required init?(coder: NSCoder) {
        fatalError("not used")
    }
}
