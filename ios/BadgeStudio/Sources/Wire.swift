import Foundation
import CoreGraphics

enum Wire {
    static let frameBytes = 466 * 466 * 2
    static let suffix = "-f7bd-485a-bd9d-92ad6ecfe93e"
    static let service = "7d2ea28a" + suffix
    static let command = "7d2ea28b" + suffix
    static let state = "7d2ea28c" + suffix
    static let photoData = "7d2ea28d" + suffix
    static let photoState = "7d2ea28e" + suffix
    static let version = "7d2ea28f" + suffix
    static func u16(_ data: Data, _ offset: Int) -> UInt16 {
        UInt16(data[offset]) | UInt16(data[offset + 1]) << 8
    }
    static func u32(_ data: Data, _ offset: Int) -> UInt32 {
        (0..<4).reduce(UInt32(0)) { $0 | UInt32(data[offset + $1]) << ($1 * 8) }
    }
    static func le(_ value: UInt32) -> Data {
        Data((0..<4).map { UInt8(truncatingIfNeeded: value >> ($0 * 8)) })
    }
    static func command(op: UInt8, sequence: UInt16, a: UInt32 = 0, b: UInt32 = 0, c: UInt32 = 0) -> Data {
        Data([1, op, UInt8(truncatingIfNeeded: sequence), UInt8(sequence >> 8)]) + le(a) + le(b) + le(c) + le(0)
    }
    static func crc32(_ data: Data) -> UInt32 {
        var crc = UInt32.max
        for byte in data {
            crc ^= UInt32(byte)
            for _ in 0..<8 { crc = (crc >> 1) ^ ((crc & 1) == 1 ? 0xedb88320 : 0) }
        }
        return ~crc
    }
    static func rgb565(_ rgba: [UInt8]) -> Data? {
        guard !rgba.isEmpty, rgba.count.isMultiple(of: 4) else { return nil }
        var bytes = Data(count: rgba.count / 2)
        bytes.withUnsafeMutableBytes { (output: UnsafeMutableRawBufferPointer) in
            for pixel in 0..<(rgba.count / 4) {
                let i = pixel * 4
                let value = UInt16(rgba[i] >> 3) << 11 | UInt16(rgba[i + 1] >> 2) << 5 | UInt16(rgba[i + 2] >> 3)
                output[pixel * 2] = UInt8(truncatingIfNeeded: value)
                output[pixel * 2 + 1] = UInt8(value >> 8)
            }
        }
        return bytes
    }
}

enum CropGeometry {
    static func offset(contentOffset: CGPoint, image: CGSize, side: CGFloat, zoom: CGFloat) -> CGSize {
        let centered = rect(image: image, side: side, zoom: zoom, offset: .zero)
        let translated = CGSize(width: (-centered.minX - contentOffset.x) / side,
                                height: (-centered.minY - contentOffset.y) / side)
        return clamp(translated, image: image, zoom: zoom)
    }
    static func clamp(_ offset: CGSize, image: CGSize, zoom: CGFloat) -> CGSize {
        let scale = max(1 / image.width, 1 / image.height) * zoom
        return CGSize(width: min(max(offset.width, -(image.width * scale - 1) / 2), (image.width * scale - 1) / 2),
                      height: min(max(offset.height, -(image.height * scale - 1) / 2), (image.height * scale - 1) / 2))
    }
    static func rect(image: CGSize, side: CGFloat, zoom: CGFloat, offset: CGSize) -> CGRect {
        let scale = max(side / image.width, side / image.height) * zoom
        let size = CGSize(width: image.width * scale, height: image.height * scale)
        let bounded = clamp(offset, image: image, zoom: zoom)
        let x: CGFloat = (side - size.width) * 0.5 + bounded.width * side
        let y: CGFloat = (side - size.height) * 0.5 + bounded.height * side
        return CGRect(origin: CGPoint(x: x, y: y), size: size)
    }
}

struct DeviceState: Equatable {
    let result: UInt8
    let ack: UInt16
    let battery: Int
    let brightness: Int
    let watch: Int
    let theme: Int
    let timeout: Int
    let asleep: Bool
    let charging: Bool
    let clockValid: Bool
    let page: Int
    let utc: UInt32
    let zone: Int32
    init?(_ data: Data) {
        guard data.count == 20, data[0] == 1, data[4] <= 100, (2...4).contains(data[5]), data[6] < 5, data[7] < 3 else { return nil }
        result = data[1]; ack = Wire.u16(data, 2)
        battery = Int(data[4]); brightness = Int(data[5]); watch = Int(data[6]); theme = Int(data[7])
        timeout = Int(Wire.u16(data, 8)); asleep = data[10] & 1 != 0
        charging = data[10] & 2 != 0; clockValid = data[10] & 4 != 0; page = Int(data[11])
        utc = Wire.u32(data, 12); zone = Int32(bitPattern: Wire.u32(data, 16))
    }
}

struct PhotoState {
    let state: UInt8
    let error: UInt8
    let id: UInt32
    let received: Int
    let total: Int
    let window: Int
    init?(_ data: Data) {
        guard data.count == 20, data[0] == 1, data[1] <= 5 else { return nil }
        state = data[1]; error = data[2]; id = Wire.u32(data, 4)
        received = Int(Wire.u32(data, 8)); total = Int(Wire.u32(data, 12)); window = Int(Wire.u32(data, 16))
        guard total == Wire.frameBytes, received <= total, (1...8192).contains(window) else { return nil }
    }
}

enum BadgeError: LocalizedError {
    case message(String)
    var errorDescription: String? { if case let .message(text) = self { return text }; return nil }
}
