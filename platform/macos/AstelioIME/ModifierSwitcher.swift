import Cocoa

/// `AstelioIME --switcher`, started at login by the LaunchAgent in Contents/Library/LaunchAgents: watches single
/// presses of left and right Command (or Option) in every app, so right Command reaches Astelio even while another
/// input source is chosen. It only looks at which keys go down and up (a listen-only event tap, which needs the
/// Input Monitoring permission); nothing typed is kept or sent anywhere.
final class ModifierSwitcher {
    private var tap: CFMachPort?
    private var retry: Timer?
    private let preferences = Preferences()

    // Device-dependent bits of the flags (NX_DEVICE*KEYMASK): which side of a modifier is down.
    private static let sideMasks: [Int64: UInt64] = [0x37: 0x08, 0x36: 0x10, 0x3A: 0x20, 0x3D: 0x40]
    private static let maxHold: TimeInterval = 1.0

    private enum Side { case left, right }
    private var pressed: (side: Side, at: TimeInterval)?
    private var used = false

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
        default:
            used = true // a key or a click between the press and the release: a shortcut, not a single press
        }
    }

    private func flagsChanged(_ event: CGEvent) {
        let code = event.getIntegerValueField(.keyboardEventKeycode)
        if ProcessInfo.processInfo.environment["ASTELIO_DEBUG"] != nil {
            NSLog("Astelio IME switcher: flags code=%lld flags=0x%llx", code, event.flags.rawValue)
        }
        let (left, right): (Int64, Int64) = preferences.tapModifier == .option ? (0x3A, 0x3D) : (0x37, 0x36)
        guard code == left || code == right, let mask = Self.sideMasks[code] else {
            used = true // Shift, Control... with the modifier is a chord
            return
        }
        let side: Side = code == left ? .left : .right
        let down = event.flags.rawValue & mask != 0
        let now = ProcessInfo.processInfo.systemUptime
        if down {
            if pressed != nil {
                used = true // both sides held
            } else {
                used = false
            }
            pressed = (side, now)
            return
        }
        defer { pressed = nil }
        guard let press = pressed, press.side == side, !used, now - press.at <= Self.maxHold else {
            return
        }
        switch side {
        case .left:
            InputSources.selectEnglish()
        case .right:
            if !InputSources.isAstelioCurrent() {
                InputSources.selectAstelio()
            }
        }
    }
}
