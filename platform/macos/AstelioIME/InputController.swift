import Carbon
import Cocoa
import InputMethodKit

/// One per text field (client). Keys go to the C++ session; what it returns is shown as marked text, committed with
/// insertText, and listed in the candidate window.
@objc(AstelioInputController)
final class AstelioInputController: IMKInputController {
    private let session: ASTSession

    // Device-dependent bits of NSEvent.modifierFlags (NX_DEVICE*KEYMASK): which side of a modifier is down.
    private static let deviceMasks: [UInt16: UInt] = [
        0x3B: 0x0000_0001, // left Control
        0x38: 0x0000_0002, // left Shift
        0x3C: 0x0000_0004, // right Shift
        0x37: 0x0000_0008, // left Command
        0x36: 0x0000_0010, // right Command
        0x3A: 0x0000_0020, // left Option
        0x3D: 0x0000_0040, // right Option
        0x3E: 0x0000_2000, // right Control
    ]

    override init!(server: IMKServer!, delegate: Any!, client inputClient: Any!) {
        session = ASTSession(engine: AppState.shared.engine)
        super.init(server: server, delegate: delegate, client: inputClient)
        session.applySettings(AppState.shared.preferences.settings())
    }

    // MARK: - Activation

    override func activateServer(_ sender: Any!) {
        super.activateServer(sender)
        AppState.shared.activeSession = session
        session.resetContext()
        session.applySettings(AppState.shared.preferences.settings())
        AppState.shared.japaneseMode = true // English is another input source now (R-03)
        if session.japaneseMode != AppState.shared.japaneseMode {
            _ = session.changeMode(toJapanese: AppState.shared.japaneseMode)
        }
    }

    override func deactivateServer(_ sender: Any!) {
        commitComposition(sender)
        AppState.shared.candidatePanel.hide()
        super.deactivateServer(sender)
    }

    /// The app asks for the uncommitted text (a click elsewhere, the field losing focus): commit it as shown.
    override func commitComposition(_ sender: Any!) {
        guard session.composing else {
            return
        }
        let text = session.currentComposition().text
        session.abandon()
        let target: IMKTextInput? = (sender as? IMKTextInput) ?? (client() as IMKTextInput?)
        if let target = target, !text.isEmpty {
            target.insertText(text, replacementRange: Self.notFound)
        }
        AppState.shared.candidatePanel.hide()
    }

    override func recognizedEvents(_ sender: Any!) -> Int {
        let mask: NSEvent.EventTypeMask = [.keyDown, .flagsChanged, .leftMouseDown, .rightMouseDown]
        return Int(truncatingIfNeeded: mask.rawValue)
    }

    // MARK: - Keys

    override func handle(_ event: NSEvent!, client sender: Any!) -> Bool {
        guard let event = event, let client = sender as? IMKTextInput else {
            return false
        }
        switch event.type {
        case .flagsChanged:
            handleModifier(event, client: client)
            return false
        case .leftMouseDown, .rightMouseDown:
            session.markChordUsed()
            return false
        case .keyDown:
            return handleKey(event, client: client)
        default:
            return false
        }
    }

    // The 英数 and かな keys of JIS keyboards (and of tools such as Karabiner-Elements that send them for a single
    // press of Command): 英数 is Apple's English input, かな stays in Astelio.
    private static let eisuKeyCode: UInt16 = 0x66
    private static let kanaKeyCode: UInt16 = 0x68

    private func handleKey(_ event: NSEvent, client: IMKTextInput) -> Bool {
        if event.keyCode == Self.eisuKeyCode || event.keyCode == Self.kanaKeyCode {
            setMode(japanese: event.keyCode == Self.kanaKeyCode, client: client)
            return true
        }
        // B-11: password fields turn on Secure Input; the keys go to the app as they are and nothing is recorded.
        if IsSecureEventInputEnabled() {
            session.setRecordingAllowed(false)
            if session.composing {
                commitComposition(client)
            }
            return false
        }
        session.setRecordingAllowed(true)
        let flags = event.modifierFlags
        guard let output = session.keyDown(
            keyCode: event.keyCode,
            shift: flags.contains(.shift),
            control: flags.contains(.control),
            option: flags.contains(.option),
            command: flags.contains(.command),
            capsLock: flags.contains(.capsLock),
            function: flags.contains(.function)
        ) else {
            return false
        }
        apply(output, client: client)
        return true
    }

    private func handleModifier(_ event: NSEvent, client: IMKTextInput) {
        let code = event.keyCode
        let down: Bool
        if let mask = Self.deviceMasks[code] {
            down = event.modifierFlags.rawValue & mask != 0
        } else {
            down = true // Caps Lock, Fn: anything else between a press and its release spoils the tap
        }
        let time = UInt64(event.timestamp * 1000)
        switch session.modifierChanged(keyCode: code, down: down, timeMs: time) {
        case .english:
            setMode(japanese: false, client: client)
        case .japanese:
            setMode(japanese: true, client: client)
        default:
            break
        }
    }

    /// R-03: left Command switches to Apple's English input (the uncommitted text is committed first), right Command
    /// to Japanese. Switching back from English is done by `AstelioIME --switcher`, which sees keys in every input source.
    private func setMode(japanese: Bool, client: IMKTextInput) {
        guard japanese else {
            commitComposition(client)
            AppState.shared.candidatePanel.hide()
            DispatchQueue.main.async {
                InputSources.selectEnglish()
            }
            return
        }
        AppState.shared.japaneseMode = true
        if !session.japaneseMode {
            apply(session.changeMode(toJapanese: true), client: client)
        }
        AppState.shared.modeIndicator.show(japanese: true, near: caretRect(client: client, index: 0))
    }

    // MARK: - Showing the text

    private static let notFound = NSRange(location: NSNotFound, length: 0)

    private func apply(_ output: ASTOutput, client: IMKTextInput) {
        var replacement = Self.notFound
        if !output.undoCommit.isEmpty {
            // B-08: the commit being undone is just before the caret; the composition takes its place.
            let length = (output.undoCommit as NSString).length
            let selection = client.selectedRange()
            if selection.location != NSNotFound && selection.location >= length {
                replacement = NSRange(location: selection.location - length, length: length)
            }
        }
        if !output.commit.isEmpty {
            client.insertText(output.commit, replacementRange: replacement)
            replacement = Self.notFound
        }
        let composition = session.currentComposition()
        if session.composing {
            client.setMarkedText(markedText(composition), selectionRange: NSRange(location: composition.cursor, length: 0),
                                 replacementRange: replacement)
        } else if output.commit.isEmpty && output.compositionChanged {
            client.setMarkedText("", selectionRange: NSRange(location: 0, length: 0), replacementRange: replacement)
        }
        showCandidates(client: client, anchor: composition.anchor)
    }

    private func markedText(_ composition: ASTComposition) -> NSAttributedString {
        let text = NSMutableAttributedString(string: composition.text)
        for span in composition.spans {
            // kTSMHiliteRawText (2), kTSMHiliteConvertedText (4), kTSMHiliteSelectedConvertedText (5)
            let style: Int
            switch span.style {
            case .focused: style = 5
            case .converted: style = 4
            default: style = 2
            }
            if let attributes = mark(forStyle: style, at: span.range) {
                for (key, value) in attributes {
                    if let name = key.base as? String {
                        text.addAttribute(NSAttributedString.Key(name), value: value, range: span.range)
                    }
                }
            }
        }
        return text
    }

    private func showCandidates(client: IMKTextInput, anchor: Int) {
        let view = session.currentCandidates()
        if view.kind == .hidden || view.items.isEmpty {
            AppState.shared.candidatePanel.hide()
            return
        }
        AppState.shared.candidatePanel.show(view, near: caretRect(client: client, index: anchor)) { [weak self] pick in
            guard let self = self, let client = self.client() else {
                return
            }
            switch pick {
            case .item(let index):
                self.apply(self.session.pick(index), client: client)
            case .related(let index):
                self.apply(self.session.pickRelated(index), client: client)
            case .category(let index):
                _ = self.session.selectCategory(index)
                self.showCandidates(client: client, anchor: self.session.currentComposition().anchor)
            }
        }
    }

    /// The screen rectangle of the line at `index` of the marked text (bottom-left origin).
    private func caretRect(client: IMKTextInput, index: Int) -> NSRect {
        var rect = NSRect.zero
        _ = client.attributes(forCharacterIndex: index, lineHeightRectangle: &rect)
        return rect
    }

    // MARK: - Menu (the input menu in the menu bar)

    /// Only the way to the settings window: every setting lives there.
    override func menu() -> NSMenu! {
        let menu = NSMenu(title: "Astelio")
        let open = NSMenuItem(title: "Astelio の設定を開く…", action: #selector(openSettings(_:)), keyEquivalent: "")
        menu.addItem(open)
        return menu
    }

    /// The settings window is this same app opened from /Applications (not the copy in Input Methods, which would start
    /// a second input method).
    @objc private func openSettings(_ sender: Any?) {
        let isSettingsCopy: (URL) -> Bool = { $0.deletingLastPathComponent().lastPathComponent != "Input Methods" }
        if let running = NSRunningApplication.runningApplications(withBundleIdentifier: AppMode.bundleID)
            .first(where: { $0.bundleURL.map(isSettingsCopy) == true }) {
            running.activate()
            return
        }
        let candidates = [URL(fileURLWithPath: "/Applications/AstelioIME.app")]
            + NSWorkspace.shared.urlsForApplications(withBundleIdentifier: AppMode.bundleID)
        guard let app = candidates.first(where: { isSettingsCopy($0) && FileManager.default.fileExists(atPath: $0.path) })
        else {
            NSLog("Astelio IME: the settings app was not found")
            return
        }
        let configuration = NSWorkspace.OpenConfiguration()
        configuration.activates = true
        NSWorkspace.shared.openApplication(at: app, configuration: configuration)
    }
}
