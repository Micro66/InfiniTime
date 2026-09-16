import SwiftUI
import UIKit

/// UIKit owns gesture recognition and focal-point zoom; SwiftUI owns the crop
/// used for export. Both use the same aspect-fill base size and normalized offset.
struct CropViewport: UIViewRepresentable {
    let image: UIImage
    let side: CGFloat
    let enabled: Bool
    @Binding var zoom: CGFloat
    @Binding var offset: CGSize

    func makeCoordinator() -> Coordinator { Coordinator(self) }

    func makeUIView(context: Context) -> UIScrollView {
        let view = UIScrollView()
        view.delegate = context.coordinator
        view.minimumZoomScale = 1
        view.maximumZoomScale = 4
        view.bounces = false
        view.bouncesZoom = false
        view.showsHorizontalScrollIndicator = false
        view.showsVerticalScrollIndicator = false
        view.contentInsetAdjustmentBehavior = .never
        view.decelerationRate = .fast
        view.addSubview(context.coordinator.imageView)
        return view
    }

    func updateUIView(_ view: UIScrollView, context: Context) {
        let coordinator = context.coordinator
        coordinator.parent = self
        view.isScrollEnabled = enabled
        view.pinchGestureRecognizer?.isEnabled = enabled
        guard side > 0 else { return }

        let layoutChanged = coordinator.imageView.image !== image || coordinator.side != side
        let rect = CropGeometry.rect(image: image.size, side: side, zoom: zoom, offset: offset)
        let desired = CGPoint(x: -rect.minX, y: -rect.minY)
        let cropChanged = abs(view.zoomScale - zoom) > 0.00001 ||
            abs(view.contentOffset.x - desired.x) > 0.01 || abs(view.contentOffset.y - desired.y) > 0.01
        guard layoutChanged || cropChanged else { return }

        coordinator.configuring = true
        defer { coordinator.configuring = false }
        if layoutChanged {
            view.setZoomScale(1, animated: false)
            coordinator.imageView.image = image
            coordinator.side = side
            let base = CropGeometry.rect(image: image.size, side: side, zoom: 1, offset: .zero).size
            coordinator.imageView.frame = CGRect(origin: .zero, size: base)
            view.contentSize = base
        }
        view.setZoomScale(zoom, animated: false)
        view.setContentOffset(desired, animated: false)
    }

    @MainActor
    final class Coordinator: NSObject, UIScrollViewDelegate {
        var parent: CropViewport
        let imageView = UIImageView()
        var side: CGFloat = 0
        var configuring = false

        init(_ parent: CropViewport) {
            self.parent = parent
            super.init()
            imageView.contentMode = .scaleToFill
        }

        func viewForZooming(in scrollView: UIScrollView) -> UIView? { imageView }
        func scrollViewDidZoom(_ scrollView: UIScrollView) { publish(scrollView) }
        func scrollViewDidScroll(_ scrollView: UIScrollView) { publish(scrollView) }

        private func publish(_ view: UIScrollView) {
            guard !configuring, parent.enabled, side > 0 else { return }
            let nextZoom = view.zoomScale
            let nextOffset = CropGeometry.offset(contentOffset: view.contentOffset, image: parent.image.size, side: side, zoom: nextZoom)
            parent.zoom = nextZoom
            parent.offset = nextOffset
        }
    }
}
