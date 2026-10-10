import Foundation

public enum WireError: Error { case size, message }
public struct Framer {
    private var buffer = Data()
    public init() {}
    public mutating func feed(_ bytes: Data) throws -> [Data] {
        guard buffer.count + bytes.count <= 65536 else { throw WireError.size }
        buffer.append(bytes)
        var messages: [Data] = []
        while buffer.count >= 4 {
            let size = buffer.prefix(4).reduce(0) { ($0 << 8) | Int($1) }
            guard size > 0, size <= 8192 else { throw WireError.size }
            guard buffer.count >= size + 4 else { break }
            messages.append(Data(buffer.dropFirst(4).prefix(size)))
            buffer = Data(buffer.dropFirst(size + 4))
        }
        return messages
    }
    public static func encode(_ object: [String: String]) throws -> Data {
        let payload = try JSONSerialization.data(withJSONObject: object)
        guard payload.count > 0, payload.count <= 8192 else { throw WireError.size }
        let size = UInt32(payload.count)
        var bytes = Data([UInt8(size >> 24), UInt8((size >> 16) & 255), UInt8((size >> 8) & 255), UInt8(size & 255)])
        bytes.append(payload)
        return bytes
    }
}
public struct Message: Decodable {
    public let type: String
    public let phase: String?
    public let x: Double?
    public let y: Double?
    public let dx: Double?
    public let dy: Double?
    public let text: String?
    public let device: String?
    public let id: String?
    public init(data: Data) throws {
        self = try JSONDecoder().decode(Self.self, from: data)
        switch type {
        case "touch", "mouse":
            guard let x, let y, x.isFinite, y.isFinite,
                  (0...1).contains(x), (0...1).contains(y),
                  let phase, ["began", "moved", "ended", "cancelled"].contains(phase) else { throw WireError.message }
        case "scroll":
            guard let dx, let dy, dx.isFinite, dy.isFinite,
                  abs(dx) <= 1000, abs(dy) <= 1000 else { throw WireError.message }
        case "text":
            guard let text, !text.isEmpty, text.utf8.count <= 4096,
                  !text.unicodeScalars.contains(where: { $0.value == 0 }) else { throw WireError.message }
        case "ready":
            guard let id, !id.isEmpty, id.count <= 64,
                  let device, !device.isEmpty, device.count <= 64 else { throw WireError.message }
        case "release", "pong": break
        default: throw WireError.message
        }
    }
}
public enum MouseAction: Equatable {
    case move, leftDown, leftDrag, leftUp, rightDown, rightDrag, rightUp
}
public struct Buttons {
    public private(set) var left = false
    public private(set) var right = false
    public init() {}
    public mutating func handle(_ message: Message) -> MouseAction? {
        guard message.type == "touch" || message.type == "mouse" else { return nil }
        let isRight = message.type == "mouse"
        switch message.phase {
        case "began":
            if isRight { guard !right else { return nil }; right = true; return .rightDown }
            guard !left else { return nil }; left = true; return .leftDown
        case "ended", "cancelled":
            if isRight { guard right else { return nil }; right = false; return .rightUp }
            guard left else { return nil }; left = false; return .leftUp
        case "moved": return left ? .leftDrag : right ? .rightDrag : .move
        default: return nil
        }
    }
    public mutating func release() -> [MouseAction] {
        var result: [MouseAction] = []
        if left { result.append(.leftUp) }
        if right { result.append(.rightUp) }
        left = false; right = false
        return result
    }
}
