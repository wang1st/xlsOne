import AppKit
import SwiftUI

struct ColumnResizeTrackingRegion: NSViewRepresentable {
    let onDragChanged: (CGFloat) -> Void
    let onDragEnded: () -> Void

    func makeNSView(context: Context) -> ColumnResizeTrackingNSView {
        let view = ColumnResizeTrackingNSView()
        view.onDragChanged = onDragChanged
        view.onDragEnded = onDragEnded
        return view
    }

    func updateNSView(_ nsView: ColumnResizeTrackingNSView, context: Context) {
        nsView.onDragChanged = onDragChanged
        nsView.onDragEnded = onDragEnded
    }
}

final class ColumnResizeTrackingNSView: NSView {
    var onDragChanged: ((CGFloat) -> Void)?
    var onDragEnded: (() -> Void)?

    private var trackingArea: NSTrackingArea?
    private var dragStartPoint: NSPoint?
    private var hasPushedDragCursor = false
    private var lastLoggedHoverPoint: NSPoint?

    override var isFlipped: Bool {
        true
    }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        wantsLayer = true
        layer?.backgroundColor = NSColor.clear.cgColor
        GridDebugLogger.log("resize-tracking init frame=\(NSStringFromRect(frameRect))")
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func hitTest(_ point: NSPoint) -> NSView? {
        let hit = bounds.contains(point)
        if hit {
            GridDebugLogger.log("resize-tracking hitTest point=\(format(point)) bounds=\(NSStringFromRect(bounds))")
        }
        return hit ? self : nil
    }

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool {
        true
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        invalidateCursorRects()
        GridDebugLogger.log("resize-tracking movedToWindow bounds=\(NSStringFromRect(bounds))")
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()

        if let trackingArea {
            removeTrackingArea(trackingArea)
        }

        let newTrackingArea = NSTrackingArea(
            rect: .zero,
            options: [.activeAlways, .mouseEnteredAndExited, .mouseMoved, .enabledDuringMouseDrag, .cursorUpdate, .inVisibleRect],
            owner: self,
            userInfo: nil
        )
        addTrackingArea(newTrackingArea)
        trackingArea = newTrackingArea
        GridDebugLogger.log("resize-tracking updateTrackingAreas bounds=\(NSStringFromRect(bounds))")
    }

    override func resetCursorRects() {
        super.resetCursorRects()
        addCursorRect(bounds, cursor: .resizeLeftRight)
        GridDebugLogger.log("resize-tracking resetCursorRects bounds=\(NSStringFromRect(bounds))")
    }

    override func mouseEntered(with event: NSEvent) {
        super.mouseEntered(with: event)
        NSCursor.resizeLeftRight.set()
        GridDebugLogger.log("resize-tracking mouseEntered point=\(format(convert(event.locationInWindow, from: nil)))")
    }

    override func mouseMoved(with event: NSEvent) {
        super.mouseMoved(with: event)
        NSCursor.resizeLeftRight.set()
        let point = convert(event.locationInWindow, from: nil)
        if shouldLogHover(point) {
            GridDebugLogger.log("resize-tracking mouseMoved point=\(format(point))")
        }
    }

    override func cursorUpdate(with event: NSEvent) {
        NSCursor.resizeLeftRight.set()
        GridDebugLogger.log("resize-tracking cursorUpdate point=\(format(convert(event.locationInWindow, from: nil)))")
    }

    override func mouseDown(with event: NSEvent) {
        dragStartPoint = convert(event.locationInWindow, from: nil)
        pushDragCursorIfNeeded()
        GridDebugLogger.log("resize-tracking mouseDown point=\(format(dragStartPoint!))")
    }

    override func mouseDragged(with event: NSEvent) {
        guard let dragStartPoint else { return }

        let currentPoint = convert(event.locationInWindow, from: nil)
        GridDebugLogger.log(
            "resize-tracking mouseDragged start=\(format(dragStartPoint)) current=\(format(currentPoint)) dx=\(String(format: "%.1f", currentPoint.x - dragStartPoint.x))"
        )
        onDragChanged?(currentPoint.x - dragStartPoint.x)
    }

    override func mouseUp(with event: NSEvent) {
        dragStartPoint = nil
        popDragCursorIfNeeded()
        onDragEnded?()
        GridDebugLogger.log("resize-tracking mouseUp point=\(format(convert(event.locationInWindow, from: nil)))")
    }

    override func mouseExited(with event: NSEvent) {
        super.mouseExited(with: event)
        if dragStartPoint == nil {
            NSCursor.arrow.set()
            popDragCursorIfNeeded()
        }
        GridDebugLogger.log("resize-tracking mouseExited point=\(format(convert(event.locationInWindow, from: nil)))")
    }

    private func invalidateCursorRects() {
        discardCursorRects()
        window?.invalidateCursorRects(for: self)
    }

    private func pushDragCursorIfNeeded() {
        guard !hasPushedDragCursor else { return }
        NSCursor.resizeLeftRight.push()
        hasPushedDragCursor = true
        GridDebugLogger.log("resize-tracking pushDragCursor")
    }

    private func popDragCursorIfNeeded() {
        guard hasPushedDragCursor else { return }
        NSCursor.pop()
        hasPushedDragCursor = false
        GridDebugLogger.log("resize-tracking popDragCursor")
    }

    deinit {
        popDragCursorIfNeeded()
        GridDebugLogger.log("resize-tracking deinit")
    }

    private func shouldLogHover(_ point: NSPoint) -> Bool {
        guard let lastPoint = lastLoggedHoverPoint else {
            self.lastLoggedHoverPoint = point
            return true
        }

        let changedEnough = abs(lastPoint.x - point.x) >= 8 || abs(lastPoint.y - point.y) >= 4
        if changedEnough {
            self.lastLoggedHoverPoint = point
        }
        return changedEnough
    }

    private func format(_ point: NSPoint) -> String {
        "(\(String(format: "%.1f", point.x)),\(String(format: "%.1f", point.y)))"
    }
}
