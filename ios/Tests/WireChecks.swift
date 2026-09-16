import Foundation
import CoreGraphics
@main
struct WireChecks {
    static func main() {
        precondition(Wire.crc32(Data("123456789".utf8)) == 0xcbf43926)
        precondition(Wire.crc32(Data()) == 0)
        let packet = Wire.command(op: 1, sequence: 0x1234, a: 0x01020304, b: UInt32(bitPattern: -12600))
        precondition(packet.count == 20 && Array(packet.prefix(8)) == [1, 1, 0x34, 0x12, 4, 3, 2, 1])
        precondition(Int32(bitPattern: Wire.u32(packet, 8)) == -12600)
        precondition(DeviceState(Data(repeating: 0, count: 20)) == nil)
        var state = Data([1, 0, 0x34, 0x12, 87, 3, 2, 1, 60, 0, 6, 7]) + Wire.le(1800000000) + Wire.le(UInt32(bitPattern: -12600))
        let decoded = DeviceState(state)!
        precondition(decoded.charging && decoded.clockValid && !decoded.asleep && decoded.zone == -12600 && decoded.ack == 0x1234)
        state[6] = 5; precondition(DeviceState(state) == nil)
        let photo = Data([1, 1, 0, 0]) + Wire.le(12) + Wire.le(100) + Wire.le(UInt32(Wire.frameBytes)) + Wire.le(8192)
        precondition(PhotoState(photo)?.received == 100)
        precondition(PhotoState(photo.dropLast()) == nil)
        precondition(Wire.rgb565([255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255]) == Data([0,248,224,7,31,0,255,255]))
        precondition(Wire.rgb565([1,2,3]) == nil)
        let landscape = CGSize(width: 1200, height: 600)
        let centered = CropGeometry.rect(image: landscape, side: 466, zoom: 1, offset: .zero)
        precondition(abs(centered.minX + 233) < 0.000001 && abs(centered.minY) < 0.000001 && abs(centered.width - 932) < 0.000001 && abs(centered.height - 466) < 0.000001)
        let bounded = CropGeometry.rect(image: landscape, side: 466, zoom: 1, offset: CGSize(width: 100, height: 100))
        precondition(abs(bounded.minX) < 0.000001 && abs(bounded.minY) < 0.000001 && bounded.maxX >= 465.999999 && bounded.maxY >= 465.999999)
        let preview = CropGeometry.rect(image: landscape, side: 300, zoom: 2.5, offset: CGSize(width: -0.2, height: 0.1))
        let output = CropGeometry.rect(image: landscape, side: 466, zoom: 2.5, offset: CGSize(width: -0.2, height: 0.1))
        precondition(abs(preview.minX / 300 - output.minX / 466) < 0.000001)
        print("PASS: CRC32 golden vector, wire endianness/validation, RGB565 primary colors, crop bounds and preview/output parity")
    }
}
