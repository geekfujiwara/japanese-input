import Cocoa

/// The candidate window: frosted glass (NSVisualEffectView) with rounded corners, near the focused segment.
/// It never takes the focus from the app; a click picks a candidate.
final class CandidatePanel {
    enum Pick {
        case item(Int)
        case related(Int)
        case category(Int)
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

    /// B-03: the candidates stand in columns of 9 (up to 3), each numbered 1-9; the digits pick from the focused column.
    private func fillCandidates(_ view: ASTCandidateView) {
        let numbered = view.kind == .candidates
        let perColumn = 9
        let columns = NSStackView()
        columns.orientation = .horizontal
        columns.alignment = .top
        columns.spacing = 6
        var column: NSStackView?
        for (index, item) in view.items.enumerated() {
            if index % perColumn == 0 {
                let next = NSStackView()
                next.orientation = .vertical
                next.alignment = .width // every row as wide as the widest, so the selection fills the line
                next.spacing = 0
                columns.addArrangedSubview(next)
                column = next
            }
            let mark = index < view.marks.count ? ASTCandidateMark(rawValue: view.marks[index].intValue) : nil
            let row = Row(number: numbered ? "\(index % perColumn + 1)" : nil, text: item,
                          note: mark == .typo ? "もしかして" : (mark == .learned ? "履歴" : nil),
                          selected: index == view.selected) { [weak self] in
                self?.onPick?(.item(index))
            }
            column?.addArrangedSubview(row)
        }
        stack.addArrangedSubview(columns)
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
        let position: String
        switch view.kind {
        case .predictions:
            position = "Tab で選ぶ"
        default:
            position = "\(view.firstIndex + max(view.selected, 0) + 1) / \(view.total)"
        }
        let hint = view.kind == .candidates && view.columns > 1 ? "← → 列の移動" : ""
        stack.addArrangedSubview(Self.footer(hint: hint, position: position))
    }

    /// B-13: the palette: a search line, a tab for each category, the grid and a footer (as the Windows version).
    private static let emojiTabIcons = ["🕘", "😀", "👋", "🐶", "🍎", "⚽", "🚗", "💡", "❤️", "🏁"]
    private static let emojiCategoryNames = ["履歴", "顔", "人", "自然", "食べ物", "活動", "旅行", "物", "記号", "旗"]

    private func fillEmoji(_ view: ASTCandidateView) {
        let search = view.active
            ? (view.title.isEmpty ? "🔍 読みで検索（例: いぬ）" : "🔍 \(view.title)")
            : "🔍 Tab か ↓ で絵文字を選ぶ"
        stack.addArrangedSubview(Self.label(search, font: .systemFont(ofSize: 13), color: .secondaryLabelColor))

        let tabs = NSStackView()
        tabs.orientation = .horizontal
        tabs.spacing = 2
        for (index, icon) in Self.emojiTabIcons.enumerated() {
            tabs.addArrangedSubview(Cell(text: icon, selected: index == view.category, width: 32, fontSize: 16) {
                [weak self] in
                self?.onPick?(.category(index))
            })
        }
        stack.addArrangedSubview(tabs)

        let columns = max(view.columns, 1)
        if view.items.isEmpty {
            let message = !view.title.isEmpty ? "見つかりません"
                : view.category == 0 ? "使った絵文字がここに並びます" : "絵文字がありません"
            stack.addArrangedSubview(Self.label(message, font: .systemFont(ofSize: 13), color: .secondaryLabelColor))
        }
        var line: NSStackView?
        for (index, emoji) in view.items.enumerated() {
            if index % columns == 0 {
                let next = NSStackView()
                next.orientation = .horizontal
                next.spacing = 2
                stack.addArrangedSubview(next)
                line = next
            }
            line?.addArrangedSubview(Cell(text: emoji, selected: index == view.selected, width: 40) { [weak self] in
                self?.onPick?(.item(index))
            })
        }

        var position = view.title.isEmpty && view.category < Self.emojiCategoryNames.count
            ? Self.emojiCategoryNames[view.category] : "検索"
        if view.selected >= 0 {
            position += "  \(view.firstIndex + view.selected + 1) / \(view.total)"
        }
        stack.addArrangedSubview(Self.footer(hint: view.active ? "Enter 決定  Tab 分類  Esc 戻る" : "Space 変換",
                                             position: position))
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

    /// A hint on the left and the position on the right.
    fileprivate static func footer(hint: String, position: String) -> NSView {
        let line = NSStackView()
        line.orientation = .horizontal
        line.distribution = .fill
        line.spacing = 16
        let left = label(hint, font: smallFont, color: .secondaryLabelColor)
        left.setContentHuggingPriority(.defaultLow, for: .horizontal)
        let right = label(position, font: smallFont, color: .secondaryLabelColor)
        right.alignment = .right
        right.setContentHuggingPriority(.required, for: .horizontal)
        line.addArrangedSubview(left)
        line.addArrangedSubview(right)
        return line
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
    init(text: String, selected: Bool, width: CGFloat = 32, fontSize: CGFloat = 20, action: @escaping () -> Void) {
        super.init(selected: selected, action: action)
        let label = CandidatePanel.label(text, font: .systemFont(ofSize: fontSize), color: .labelColor)
        label.alignment = .center
        label.translatesAutoresizingMaskIntoConstraints = false
        addSubview(label)
        NSLayoutConstraint.activate([
            widthAnchor.constraint(equalToConstant: width),
            heightAnchor.constraint(equalToConstant: 30),
            label.centerXAnchor.constraint(equalTo: centerXAnchor),
            label.centerYAnchor.constraint(equalTo: centerYAnchor),
        ])
    }

    required init?(coder: NSCoder) {
        fatalError("not used")
    }
}
