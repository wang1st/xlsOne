import AppKit
import SwiftUI

struct HeaderDividerCursorOverlay: NSViewRepresentable {
    let renderedColumnWidths: [CGFloat]
    let onDragChanged: (Int, CGFloat) -> Void
    let onDragEnded: (Int) -> Void

    func makeNSView(context: Context) -> HeaderDividerCursorOverlayNSView {
        let view = HeaderDividerCursorOverlayNSView()
        view.renderedColumnWidths = renderedColumnWidths
        view.onDragChanged = onDragChanged
        view.onDragEnded = onDragEnded
        return view
    }

    func updateNSView(_ nsView: HeaderDividerCursorOverlayNSView, context: Context) {
        nsView.renderedColumnWidths = renderedColumnWidths
        nsView.onDragChanged = onDragChanged
        nsView.onDragEnded = onDragEnded
    }
}

struct HeaderDividerInteractionModel {
    let renderedColumnWidths: [CGFloat]
    let hitWidth: CGFloat

    init(
        renderedColumnWidths: [CGFloat],
        hitWidth: CGFloat = GridMetrics.resizeHandleHitWidth
    ) {
        self.renderedColumnWidths = renderedColumnWidths
        self.hitWidth = hitWidth
    }

    func dividerIndex(at x: CGFloat) -> Int? {
        var edgeX: CGFloat = 0
        let halfBand = hitWidth / 2

        for (index, width) in renderedColumnWidths.enumerated() {
            edgeX += width
            if x >= edgeX - halfBand && x <= edgeX + halfBand {
                return index
            }
        }

        return nil
    }

    func cursorRects(in bounds: NSRect) -> [NSRect] {
        guard bounds.width > 0, bounds.height > 0 else { return [] }

        var rects: [NSRect] = []
        var edgeX: CGFloat = 0
        let halfBand = hitWidth / 2

        for width in renderedColumnWidths {
            edgeX += width
            let originX = max(0, edgeX - halfBand)
            let rectWidth = min(hitWidth, max(0, bounds.width - originX))
            guard rectWidth > 0 else { continue }

            rects.append(NSRect(x: originX, y: 0, width: rectWidth, height: bounds.height))
        }

        return rects
    }
}

final class HeaderDividerCursorOverlayNSView: NSView {
    var renderedColumnWidths: [CGFloat] = [] {
        didSet {
            if oldValue != renderedColumnWidths {
                GridDebugLogger.log("header-overlay widths=\(renderedColumnWidths.map { String(format: "%.1f", $0) }.joined(separator: ","))")
                invalidateDividerCursorRects()
            }
        }
    }
    var onDragChanged: ((Int, CGFloat) -> Void)?
    var onDragEnded: ((Int) -> Void)?
    private(set) var registeredDividerCursorRects: [NSRect] = []
    private(set) var activeTrackingAreaOptions: NSTrackingArea.Options = []

    private var trackingArea: NSTrackingArea?
    private var dragState: (column: Int, startPoint: NSPoint)?
    private var hasPushedDragCursor = false
    private var lastBounds: NSRect = .zero
    private weak var observedWindow: NSWindow?
    private var previousAcceptsMouseMovedEvents = false

    override var isFlipped: Bool {
        true
    }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        wantsLayer = true
        layer?.backgroundColor = NSColor.clear.cgColor
        lastBounds = bounds
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    // MARK: - Frame / Tracking Area Fix
    // SwiftUI sets frame directly (bypassing layout()), so we override setFrameSize
    // to update the tracking area whenever our bounds change.

    override func setFrameSize(_ newSize: NSSize) {
        super.setFrameSize(newSize)
        if bounds != lastBounds {
            lastBounds = bounds
            updateTrackingAreas()
            invalidateDividerCursorRects()
        }
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        observedWindow?.acceptsMouseMovedEvents = previousAcceptsMouseMovedEvents
        observedWindow = window
        previousAcceptsMouseMovedEvents = observedWindow?.acceptsMouseMovedEvents ?? false
        observedWindow?.acceptsMouseMovedEvents = true
        invalidateDividerCursorRects()
        updateCursorForCurrentMouseLocation()
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()

        if let trackingArea {
            removeTrackingArea(trackingArea)
        }

        let newTrackingArea = NSTrackingArea(
            rect: .zero,
            options: [.activeInKeyWindow, .mouseMoved, .mouseEnteredAndExited, .enabledDuringMouseDrag, .cursorUpdate, .inVisibleRect],
            owner: self,
            userInfo: nil
        )
        addTrackingArea(newTrackingArea)
        trackingArea = newTrackingArea
        activeTrackingAreaOptions = newTrackingArea.options
        GridDebugLogger.log("header-overlay updateTrackingAreas bounds=\(NSStringFromRect(bounds))")
    }

    override func resetCursorRects() {
        super.resetCursorRects()

        registeredDividerCursorRects = interactionModel.cursorRects(in: bounds)
        for rect in registeredDividerCursorRects {
            addCursorRect(rect, cursor: .resizeLeftRight)
        }

        GridDebugLogger.log("header-overlay resetCursorRects rects=\(registeredDividerCursorRects.map(NSStringFromRect).joined(separator: ","))")
    }

    // MARK: - Hit Test

    override func hitTest(_ point: NSPoint) -> NSView? {
        let shouldHandle = dragState != nil || interactionModel.dividerIndex(at: point.x) != nil
        if shouldHandle {
            GridDebugLogger.log("header-overlay hitTest point=\(format(point)) bounds=\(NSStringFromRect(bounds))")
        }
        return shouldHandle ? self : nil
    }

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool {
        true
    }

    // MARK: - Mouse Events

    override func mouseEntered(with event: NSEvent) {
        super.mouseEntered(with: event)
        updateCursor(at: convert(event.locationInWindow, from: nil))
    }

    override func mouseMoved(with event: NSEvent) {
        super.mouseMoved(with: event)
        updateCursor(at: convert(event.locationInWindow, from: nil))
    }

    override func cursorUpdate(with event: NSEvent) {
        updateCursor(at: convert(event.locationInWindow, from: nil))
    }

    override func mouseDown(with event: NSEvent) {
        let point = convert(event.locationInWindow, from: nil)
        guard let dividerIndex = interactionModel.dividerIndex(at: point.x) else {
            return
        }

        dragState = (dividerIndex, point)
        pushDragCursorIfNeeded()
        GridDebugLogger.log("header-overlay mouseDown point=\(format(point)) divider=\(dividerIndex)")
    }

    override func mouseDragged(with event: NSEvent) {
        guard let dragState else { return }

        let point = convert(event.locationInWindow, from: nil)
        let translation = point.x - dragState.startPoint.x
        onDragChanged?(dragState.column, translation)
        GridDebugLogger.log(
            "header-overlay mouseDragged point=\(format(point)) divider=\(dragState.column) dx=\(String(format: "%.1f", translation))"
        )
    }

    override func mouseUp(with event: NSEvent) {
        guard let dragState else {
            super.mouseUp(with: event)
            return
        }

        let point = convert(event.locationInWindow, from: nil)
        let endedColumn = dragState.column
        self.dragState = nil
        popDragCursorIfNeeded()
        onDragEnded?(endedColumn)
        GridDebugLogger.log("header-overlay mouseUp point=\(format(point)) divider=\(endedColumn)")
        updateCursor(at: point)
    }

    override func mouseExited(with event: NSEvent) {
        super.mouseExited(with: event)
        if dragState == nil {
            NSCursor.arrow.set()
        }
        popDragCursorIfNeeded()
        GridDebugLogger.log("header-overlay mouseExited")
    }

    // MARK: - Cursor

    private var interactionModel: HeaderDividerInteractionModel {
        HeaderDividerInteractionModel(renderedColumnWidths: renderedColumnWidths)
    }

    private func updateCursor(at point: NSPoint) {
        guard dragState == nil else { return }
        guard bounds.contains(point) else {
            NSCursor.arrow.set()
            GridDebugLogger.log("header-overlay cursor=arrow reason=outside point=\(format(point))")
            return
        }

        let isOverDivider = interactionModel.dividerIndex(at: point.x) != nil
        if isOverDivider {
            NSCursor.resizeLeftRight.set()
            GridDebugLogger.log("header-overlay cursor=resize point=\(format(point))")
        } else {
            NSCursor.arrow.set()
            GridDebugLogger.log("header-overlay cursor=arrow point=\(format(point))")
        }
    }

    private func invalidateDividerCursorRects() {
        registeredDividerCursorRects = []
        discardCursorRects()
        window?.invalidateCursorRects(for: self)
    }

    private func updateCursorForCurrentMouseLocation() {
        guard let window else { return }
        let point = convert(window.mouseLocationOutsideOfEventStream, from: nil)
        updateCursor(at: point)
    }

    private func pushDragCursorIfNeeded() {
        guard !hasPushedDragCursor else { return }
        NSCursor.resizeLeftRight.push()
        hasPushedDragCursor = true
        GridDebugLogger.log("header-overlay cursor=resize push")
    }

    private func popDragCursorIfNeeded() {
        guard hasPushedDragCursor else { return }
        NSCursor.pop()
        hasPushedDragCursor = false
        GridDebugLogger.log("header-overlay cursor=pop")
    }

    private func format(_ point: NSPoint) -> String {
        "(\(String(format: "%.1f", point.x)),\(String(format: "%.1f", point.y)))"
    }

    deinit {
        popDragCursorIfNeeded()
        observedWindow?.acceptsMouseMovedEvents = previousAcceptsMouseMovedEvents
    }
}
