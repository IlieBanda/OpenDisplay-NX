import XCTest
@testable import CompanionCore
final class WireTests: XCTestCase {
    func testFragmentationAndCoalescing() throws {
        let bytes = try Framer.encode(["type": "text", "text": "Привет 中文 🙂"])
        var framer = Framer()
        for byte in bytes.dropLast() { XCTAssertTrue(try framer.feed(Data([byte])).isEmpty) }
        let messages = try framer.feed(Data([bytes.last!]) + bytes)
        XCTAssertEqual(messages.count, 2)
        XCTAssertEqual(try Message(data: messages[0]).text, "Привет 中文 🙂")
    }
    func testMalformedAndOversizedFrames() {
        for bytes in [Data([0,0,0,0]), Data([0,0,32,1]), Data([255,255,255,255])] {
            var framer = Framer()
            XCTAssertThrowsError(try framer.feed(bytes))
        }
        for json in ["{}", "{\"type\":\"touch\",\"phase\":\"began\",\"x\":2,\"y\":0}",
                     "{\"type\":\"mouse\",\"phase\":\"unknown\",\"x\":0,\"y\":0}",
                     "{\"type\":\"scroll\",\"dx\":1001,\"dy\":0}",
                     "{\"type\":\"text\",\"text\":\"\"}", "{\"type\":\"ready\"}"] {
            XCTAssertThrowsError(try Message(data: Data(json.utf8)))
        }
    }
    func testBothMouseButtonsAndRelease() throws {
        func event(_ type: String, _ phase: String) throws -> Message {
            try Message(data: JSONSerialization.data(withJSONObject: ["type": type, "phase": phase, "x": 0.5, "y": 0.5]))
        }
        var buttons = Buttons()
        XCTAssertEqual(buttons.handle(try event("touch", "moved")), .move)
        XCTAssertEqual(buttons.handle(try event("mouse", "began")), .rightDown)
        XCTAssertNil(buttons.handle(try event("mouse", "began")))
        XCTAssertEqual(buttons.handle(try event("touch", "moved")), .rightDrag)
        XCTAssertEqual(buttons.handle(try event("touch", "began")), .leftDown)
        XCTAssertEqual(buttons.handle(try event("touch", "moved")), .leftDrag)
        XCTAssertEqual(buttons.release(), [.leftUp, .rightUp])
        XCTAssertTrue(buttons.release().isEmpty)
        XCTAssertNil(buttons.handle(try event("mouse", "ended")))
    }
}
