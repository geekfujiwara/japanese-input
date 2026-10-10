import XCTest

/// The Objective-C++ bridge as the input controller uses it. The behavior itself is tested in
/// platform/macos/engine/tests (gtest); these check that types and indices cross the bridge intact.
final class BridgeTests: XCTestCase {
    private var directory: URL!
    private var session: ASTSession!

    // Key codes of the US layout.
    private let a: UInt16 = 0x00
    private let i: UInt16 = 0x22
    private let k: UInt16 = 0x28
    private let space: UInt16 = 0x31
    private let returnKey: UInt16 = 0x24
    private let leftCommand: UInt16 = 0x37
    private let rightCommand: UInt16 = 0x36

    override func setUpWithError() throws {
        directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        // A dictionary given by CI (ASTELIO_TEST_DICTIONARY) also checks conversion; without it only kana is typed.
        let dictionary = ProcessInfo.processInfo.environment["ASTELIO_TEST_DICTIONARY"]
        let engine = ASTEngine(dictionaryPath: dictionary, dataDirectory: directory.path)
        session = ASTSession(engine: engine)
    }

    override func tearDownWithError() throws {
        try? FileManager.default.removeItem(at: directory)
    }

    private func press(_ code: UInt16, command: Bool = false) -> ASTOutput? {
        session.keyDown(keyCode: code, shift: false, control: false, option: false, command: command,
                        capsLock: false, function: false)
    }

    func testTypingShowsKanaAsMarkedText() {
        XCTAssertNotNil(press(k))
        XCTAssertNotNil(press(a))
        XCTAssertNotNil(press(i))
        let composition = session.currentComposition()
        XCTAssertEqual(composition.text, "かい")
        XCTAssertEqual(composition.cursor, 2)
        XCTAssertEqual(composition.spans.count, 1)
        XCTAssertEqual(composition.spans.first?.style, .input)
        XCTAssertEqual(composition.spans.first?.range, NSRange(location: 0, length: 2))
    }

    func testReturnCommits() {
        _ = press(a)
        let output = press(returnKey)
        XCTAssertEqual(output?.commit, "あ")
        XCTAssertFalse(session.composing)
    }

    func testShortcutsAreNotHandled() {
        XCTAssertNil(press(a, command: true))
        XCTAssertNil(press(returnKey)) // nothing to commit: the app gets Return
    }

    func testCommandTapsSwitchTheMode() {
        XCTAssertEqual(session.modifierChanged(keyCode: leftCommand, down: true, timeMs: 0), .noChange)
        XCTAssertEqual(session.modifierChanged(keyCode: leftCommand, down: false, timeMs: 50), .english)
        _ = session.changeMode(toJapanese: false)
        XCTAssertFalse(session.japaneseMode)
        XCTAssertNil(press(a))
        XCTAssertEqual(session.modifierChanged(keyCode: rightCommand, down: true, timeMs: 100), .noChange)
        XCTAssertEqual(session.modifierChanged(keyCode: rightCommand, down: false, timeMs: 150), .japanese)
    }

    func testLeavingJapaneseCommits() {
        _ = press(a)
        XCTAssertEqual(session.changeMode(toJapanese: false).commit, "あ")
    }

    func testSettingsCrossTheBridge() {
        let settings = ASTSettings()
        XCTAssertEqual(settings.tapModifier, .command)
        XCTAssertTrue(settings.learningEnabled)
        settings.tapModifier = .option
        session.applySettings(settings)
        XCTAssertEqual(session.modifierChanged(keyCode: leftCommand, down: true, timeMs: 0), .noChange)
        XCTAssertEqual(session.modifierChanged(keyCode: leftCommand, down: false, timeMs: 50), .noChange)
    }

    func testConversionWithTheDictionary() throws {
        guard ProcessInfo.processInfo.environment["ASTELIO_TEST_DICTIONARY"] != nil else {
            throw XCTSkip("ASTELIO_TEST_DICTIONARY is not set")
        }
        for code: UInt16 in [k, a, i] {
            _ = press(code)
        }
        _ = press(space)
        let composition = session.currentComposition()
        XCTAssertNotEqual(composition.text, "かい")
        XCTAssertEqual(composition.spans.first?.style, .focused)
        _ = press(space)
        let candidates = session.currentCandidates()
        XCTAssertEqual(candidates.kind, .candidates)
        XCTAssertGreaterThan(candidates.items.count, 1)
        XCTAssertEqual(candidates.items.count, candidates.marks.count)
    }
}
