import AppKit
import CoreGraphics
import CompanionCore

final class Injector {
    private var buttons = Buttons()
    private let source = CGEventSource(stateID: .hidSystemState)
    private var lastPoint = CGPoint.zero
    private var lastClick = CFAbsoluteTime(0)
    private var lastClickPoint = CGPoint.zero
    private var clickCount: Int64 = 0
    private var activeClickCount: Int64 = 1
    var display: CGDirectDisplayID = 0
    func handle(_ message: Message) {
        guard display != 0, CGDisplayIsActive(display) != 0 else { return }
        switch message.type {
        case "touch", "mouse":
            let bounds = CGDisplayBounds(display)
            let point = CGPoint(x: bounds.minX + message.x! * max(0, bounds.width - 1),
                                y: bounds.minY + message.y! * max(0, bounds.height - 1))
            if let action = buttons.handle(message) { post(action, at: point, cancelled: message.phase == "cancelled") }
        case "scroll":
            let bounds = CGDisplayBounds(display)
            let scale = bounds.width > 0 ? Double(CGDisplayPixelsWide(display)) / bounds.width : 1
            CGEvent(scrollWheelEvent2Source: source, units: .pixel, wheelCount: 2,
                    wheel1: Int32((message.dy! / scale).rounded()),
                    wheel2: Int32((message.dx! / scale).rounded()), wheel3: 0)?.post(tap: .cghidEventTap)
        case "text": typeText(message.text!)
        case "release": release()
        default: break
        }
    }
    func release() {
        for action in buttons.release() { post(action, at: lastPoint, cancelled: true) }
    }
    private func post(_ action: MouseAction, at point: CGPoint, cancelled: Bool) {
        let eventType: CGEventType
        let button: CGMouseButton
        switch action {
        case .move: eventType = .mouseMoved; button = .left
        case .leftDown: eventType = .leftMouseDown; button = .left
        case .leftDrag: eventType = .leftMouseDragged; button = .left
        case .leftUp: eventType = .leftMouseUp; button = .left
        case .rightDown: eventType = .rightMouseDown; button = .right
        case .rightDrag: eventType = .rightMouseDragged; button = .right
        case .rightUp: eventType = .rightMouseUp; button = .right
        }
        if action == .leftDown {
            let now = CFAbsoluteTimeGetCurrent()
            clickCount = now - lastClick <= NSEvent.doubleClickInterval &&
                hypot(point.x - lastClickPoint.x, point.y - lastClickPoint.y) <= 4 ? min(clickCount + 1, 3) : 1
            activeClickCount = clickCount
            lastClick = now; lastClickPoint = point
        }
        guard let event = CGEvent(mouseEventSource: source, mouseType: eventType,
                                  mouseCursorPosition: point, mouseButton: button) else { return }
        event.setIntegerValueField(.mouseEventClickState, value: cancelled ? 0 : button == .left ? activeClickCount : 1)
        event.post(tap: .cghidEventTap)
        lastPoint = point
    }
    private func typeText(_ text: String) {
        let units = Array(text.utf16)
        var start = 0
        while start < units.count {
            var end = min(start + 20, units.count)
            if end < units.count && (0xD800...0xDBFF).contains(units[end - 1]) { end -= 1 }
            let chunk = Array(units[start..<end])
            for down in [true, false] {
                if let event = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: down) {
                    chunk.withUnsafeBufferPointer { event.keyboardSetUnicodeString(stringLength: chunk.count, unicodeString: $0.baseAddress!) }
                    event.post(tap: .cghidEventTap)
                }
            }
            start = end
        }
    }
}
