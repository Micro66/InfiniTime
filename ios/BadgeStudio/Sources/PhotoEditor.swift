import SwiftUI
import PhotosUI
import ImageIO

enum PhotoRenderer {
    static func load(_ data: Data) throws -> UIImage {
        guard let source = CGImageSourceCreateWithData(data as CFData, nil),
              let thumbnail = CGImageSourceCreateThumbnailAtIndex(source, 0, [
                kCGImageSourceCreateThumbnailFromImageAlways: true,
                kCGImageSourceCreateThumbnailWithTransform: true,
                kCGImageSourceThumbnailMaxPixelSize: 2048
              ] as CFDictionary) else { throw BadgeError.message("无法读取这张照片，请选择其他图片。") }
        return UIImage(cgImage: thumbnail)
    }
    static func render(_ image: UIImage, zoom: CGFloat, offset: CGSize) throws -> Data {
        let side: CGFloat = 466
        let format = UIGraphicsImageRendererFormat(); format.scale = 1; format.opaque = true
        let rendered = UIGraphicsImageRenderer(size: CGSize(width: side, height: side), format: format).image { context in
            UIColor.black.setFill(); context.fill(CGRect(x: 0, y: 0, width: side, height: side))
            image.draw(in: CropGeometry.rect(image: image.size, side: side, zoom: zoom, offset: offset))
        }
        guard let cg = rendered.cgImage else { throw BadgeError.message("照片裁剪失败。") }
        var rgba = [UInt8](repeating: 0, count: 466 * 466 * 4)
        let colorSpace = CGColorSpace(name: CGColorSpace.sRGB)!
        let ok = rgba.withUnsafeMutableBytes { bytes -> Bool in
            guard let context = CGContext(data: bytes.baseAddress, width: 466, height: 466, bitsPerComponent: 8, bytesPerRow: 466 * 4,
                                          space: colorSpace, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue) else { return false }
            context.draw(cg, in: CGRect(x: 0, y: 0, width: side, height: side))
            return true
        }
        guard ok else { throw BadgeError.message("照片转换失败，内存不足。") }
        guard let bytes = Wire.rgb565(rgba) else { throw BadgeError.message("照片像素数据不完整。") }
        return bytes
    }
}

struct PhotoEditor: View {
    @EnvironmentObject private var badge: BadgeConnection
    @State private var selection: PhotosPickerItem?
    @State private var image: UIImage?
    @State private var zoom: CGFloat = 1
    @State private var offset: CGSize = .zero
    @State private var dragOrigin: CGSize?
    @State private var loading = false
    @State private var error: String?

    var body: some View {
        let pickerTitle = image == nil ? "选择照片" : "换一张照片"
        ScrollView {
            VStack(alignment: .leading, spacing: 22) {
                Text("把喜欢的，\n戴在身上。")
                    .font(.system(size: 34, weight: .bold, design: .rounded))
                Text("拖动调整构图，用滑杆缩放。照片直接通过蓝牙发送，不经过云端。")
                    .font(.subheadline).foregroundStyle(Palette.muted)
                GeometryReader { proxy in
                    let side = proxy.size.width
                    ZStack {
                        Circle().fill(Palette.card)
                        if let image {
                            let rect = CropGeometry.rect(image: image.size, side: side, zoom: zoom, offset: offset)
                            Image(uiImage: image).resizable()
                                .frame(width: rect.width, height: rect.height)
                                .position(x: rect.midX, y: rect.midY)
                        } else {
                            VStack(spacing: 12) {
                                Image(systemName: "photo.on.rectangle.angled").font(.system(size: 46, weight: .light))
                                Text("你的下一枚吧唧").font(.headline)
                            }.foregroundStyle(Palette.muted)
                        }
                    }
                    .frame(width: side, height: side).clipShape(Circle())
                    .overlay(Circle().stroke(Palette.accent.opacity(0.35), lineWidth: 2))
                    .contentShape(Circle())
                    .gesture(DragGesture().onChanged { value in
                        guard let image, !badge.transferring else { return }
                        if dragOrigin == nil { dragOrigin = offset }
                        offset = CropGeometry.clamp(CGSize(width: dragOrigin!.width + value.translation.width / side,
                                                           height: dragOrigin!.height + value.translation.height / side), image: image.size, zoom: zoom)
                    }.onEnded { _ in dragOrigin = nil })
                }.aspectRatio(1, contentMode: .fit)
                HStack {
                    Image(systemName: "minus.magnifyingglass")
                    Slider(value: $zoom, in: 1...4).tint(Palette.accent)
                        .onChange(of: zoom) { _, _ in if let image { offset = CropGeometry.clamp(offset, image: image.size, zoom: zoom) } }
                    Image(systemName: "plus.magnifyingglass")
                }.disabled(image == nil || badge.transferring).foregroundStyle(Palette.muted)
                PhotosPicker(selection: $selection, matching: .images, photoLibrary: .shared()) {
                    Label(pickerTitle, systemImage: "photo.badge.plus")
                        .frame(maxWidth: .infinity).padding(16).contentShape(RoundedRectangle(cornerRadius: 18))
                }.buttonStyle(.plain).background(Palette.card, in: RoundedRectangle(cornerRadius: 18))
                    .disabled(loading || badge.transferring)
                    .onChange(of: selection) { _, item in
                        Task {
                            guard let item else { return }
                            loading = true; error = nil
                            defer { loading = false }
                            do {
                                guard let data = try await item.loadTransferable(type: Data.self) else { throw BadgeError.message("照片尚未下载，请稍后重试。") }
                                image = try PhotoRenderer.load(data); zoom = 1; offset = .zero
                            } catch { self.error = error.localizedDescription }
                        }
                    }
                if loading { ProgressView("正在读取照片").tint(Palette.accent) }
                if badge.transferring {
                    VStack(spacing: 12) {
                        ProgressView(value: badge.progress).tint(Palette.accent)
                        Text("\(Int(badge.progress * 100))% · \(badge.transferText)").font(.caption)
                        if badge.canCancelTransfer {
                            Button(role: .destructive) { badge.cancelPhoto() } label: {
                                Text("停止发送").frame(maxWidth: .infinity).frame(minHeight: 44).contentShape(Rectangle())
                            }
                        }
                    }.padding(18).background(Palette.card, in: RoundedRectangle(cornerRadius: 18))
                } else {
                    Button {
                        guard let image else { return }
                        do { badge.sendPhoto(try PhotoRenderer.render(image, zoom: zoom, offset: offset)) }
                        catch { self.error = error.localizedDescription }
                    } label: {
                        Label("发送到吧唧", systemImage: "arrow.up.right")
                            .font(.headline).frame(maxWidth: .infinity).padding(18).contentShape(RoundedRectangle(cornerRadius: 18))
                    }.buttonStyle(.plain).background(Palette.accent, in: RoundedRectangle(cornerRadius: 18)).foregroundStyle(Palette.background)
                        .disabled(image == nil || !badge.ready || loading || badge.working)
                        .opacity(image != nil && badge.ready ? 1 : 0.4)
                    if !badge.transferText.isEmpty { Text(badge.transferText).font(.subheadline).foregroundStyle(Palette.accent) }
                }
                if !badge.ready { Text("请先在“设备”页连接吧唧。").foregroundStyle(Palette.muted).font(.footnote) }
                if let error { Text(error).foregroundStyle(.orange).font(.footnote) }
            }.padding(24)
        }.background(Palette.background).navigationTitle("照片吧唧").navigationBarTitleDisplayMode(.inline)
    }
}
