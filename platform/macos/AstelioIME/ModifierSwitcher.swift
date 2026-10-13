import Cocoa

/// `AstelioIME --switcher`, started at login by the LaunchAgent in Contents/Library/LaunchAgents: watches single
/// presses of left and right Command (or Option) in every app, so right Command reaches Astelio even while another
/// input source is chosen. It only looks at which keys go down and up (a listen-only event tap, which needs the
/// Input Monitoring permission); nothing typed is kept or sent anywhere.
final class ModifierSwitcher {
    private var tap: CFMachPort?
    private var retry: Timer?
    private let preferences = Preferences()

    private static let maxHold: TimeInterval = 1.0
    private static let leftShift: Int64 = 0x38
    private static let rightShift: Int64 = 0x3C

    private enum Key { case left, right, shift }
    private var pressed: (key: Key, at: TimeInterval)?
    private var used = false
    private var debug = ProcessInfo.processInfo.environment["ASTELIO_DEBUG"] != nil

    func start() {
        if !installTap() {
            NSLog("Astelio IME: waiting for the Input Monitoring permission to switch input sources")
            retry = Timer.scheduledTimer(withTimeInterval: 3, repeats: true) { [weak self] timer in
                if self?.installTap() == true {
                    timer.invalidate()
                }
            }
        }
    }

    private func installTap() -> Bool {
        guard tap == nil else {
            return true
        }
        let types: [CGEventType] = [.flagsChanged, .keyDown, .leftMouseDown, .rightMouseDown, .otherMouseDown,
                                    .scrollWheel]
        let mask = types.reduce(CGEventMask(0)) { $0 | (CGEventMask(1) << CGEventMask($1.rawValue)) }
        let callback: CGEventTapCallBack = { _, type, event, info in
            if let info {
                Unmanaged<ModifierSwitcher>.fromOpaque(info).takeUnretainedValue().handle(type, event)
            }
            return Unmanaged.passUnretained(event)
        }
        guard let tap = CGEvent.tapCreate(tap: .cgSessionEventTap, place: .tailAppendEventTap, options: .listenOnly,
                                          eventsOfInterest: mask, callback: callback,
                                          userInfo: Unmanaged.passUnretained(self).toOpaque()) else {
            return false
        }
        let source = CFMachPortCreateRunLoopSource(nil, tap, 0)
        CFRunLoopAddSource(CFRunLoopGetMain(), source, .commonModes)
        CGEvent.tapEnable(tap: tap, enable: true)
        self.tap = tap
        NSLog("Astelio IME: switching input sources with single presses of Command")
        return true
    }

    private func handle(_ type: CGEventType, _ event: CGEvent) {
        switch type {
        case .tapDisabledByTimeout, .tapDisabledByUserInput:
            if let tap {
                CGEvent.tapEnable(tap: tap, enable: true)
            }
        case .flagsChanged:
            flagsChanged(event)
        case .keyDown where [0x66, 0x68].contains(event.getIntegerValueField(.keyboardEventKeycode)):
            // 英数 / かな (JIS keys, or sent by Karabiner-Elements for a single press of Command): macOS answers them
            // with its own choice of input source, so choose ours just after it, twice in case macOS is slow.
            used = true
            let english = event.getIntegerValueField(.keyboardEventKeycode) == 0x66
            for delay in [0.15, 0.5] {
                DispatchQueue.main.asyncAfter(deadline: .now() + delay) {
                    if english {
                        InputSources.selectEnglish()
                    } else {
                        InputSources.selectAstelio()
                    }
                }
            }
        default:
            used = true // a key or a click between the press and the release: a shortcut, not a single press
        }
    }

    /// Which modifier went down or up. Tools such as Karabiner-Elements send the modifiers without the bits that tell
    /// left from right, so the key code says which key changed, and the flags whether it is now down.
    private func flagsChanged(_ event: CGEvent) {
        let code = event.getIntegerValueField(.keyboardEventKeycode)
        let flags = event.flags
        if debug {
            NSLog("Astelio IME switcher: flags code=%lld flags=0x%llx", code, flags.rawValue)
        }
        let (left, right): (Int64, Int64) = preferences.tapModifier == .option ? (0x3A, 0x3D) : (0x37, 0x36)
        let key: Key
        let down: Bool
        switch code {
        case left, right:
            key = code == left ? .left : .right
            let generic: CGEventFlags = preferences.tapModifier == .option ? .maskAlternate : .maskCommand
            let sideBits: [Int64: UInt64] = [0x37: 0x08, 0x36: 0x10, 0x3A: 0x20, 0x3D: 0x40]
            let side = sideBits[code] ?? 0
            let anySide = flags.rawValue & (sideBits[left]! | sideBits[right]!) != 0
            down = anySide ? flags.rawValue & side != 0 : flags.contains(generic)
        case Self.leftShift, Self.rightShift:
            key = .shift
            let side: UInt64 = code == Self.leftShift ? 0x02 : 0x04
            let anySide = flags.rawValue & 0x06 != 0
            down = anySide ? flags.rawValue & side != 0 : flags.contains(.maskShift)
        default:
            used = true // Control, Fn... with the modifier is a chord
            return
        }
        let now = ProcessInfo.processInfo.systemUptime
        if down {
            used = pressed != nil // two modifiers held together
            pressed = (key, now)
            return
        }
        defer { pressed = nil }
        guard let press = pressed, press.key == key, !used, now - press.at <= Self.maxHold else {
            return
        }
        switch key {
        case .left:
            InputSources.selectEnglish()
        case .right:
            InputSources.selectAstelio()
        case .shift:
            DistributedNotificationCenter.default().postNotificationName(
                AppMode.shiftTapNotification, object: nil, userInfo: nil, deliverImmediately: true)
        }
    }
}
