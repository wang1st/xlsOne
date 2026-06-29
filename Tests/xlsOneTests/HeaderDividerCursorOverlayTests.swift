import AppKit
import XCTest
@testable import xlsOneUI

@MainActor
final class HeaderDividerCursorOverlayTests: XCTestCase {
    func testResetCursorRectsRegistersResizeCursorBandsForVisibleDividers() {
        let view = HeaderDividerCursorOverlayNSView(
            frame: NSRect(x: 0, y: 0, width: 210, height: 24)
        )
        view.renderedColumnWidths = [80, 90, 100]

        view.resetCursorRects()

        XCTAssertEqual(
            view.registeredDividerCursorRects,
            [
                NSRect(x: 44, y: 0, width: 72, height: 24),
                NSRect(x: 134, y: 0, width: 72, height: 24)
            ]
        )
    }

    func testChangingWidthsInvalidatesCursorRectsUntilTheyAreRebuilt() {
        let view = HeaderDividerCursorOverlayNSView(
            frame: NSRect(x: 0, y: 0, width: 260, height: 24)
        )
        view.renderedColumnWidths = [80, 90, 100]
        view.resetCursorRects()

        XCTAssertEqual(view.registeredDividerCursorRects.count, 3)

        view.renderedColumnWidths = [120, 60, 80]

        XCTAssertTrue(view.registeredDividerCursorRects.isEmpty)

        view.resetCursorRects()

        XCTAssertEqual(
            view.registeredDividerCursorRects,
            [
                NSRect(x: 84, y: 0, width: 72, height: 24),
                NSRect(x: 144, y: 0, width: 72, height: 24),
                NSRect(x: 224, y: 0, width: 36, height: 24)
            ]
        )
    }

    func testTrackingAreaUsesKeyWindowVisibleRectMouseMoveConfiguration() {
        let view = HeaderDividerCursorOverlayNSView(
            frame: NSRect(x: 0, y: 0, width: 260, height: 24)
        )

        view.updateTrackingAreas()

        XCTAssertTrue(view.activeTrackingAreaOptions.contains(.activeInKeyWindow))
        XCTAssertTrue(view.activeTrackingAreaOptions.contains(.inVisibleRect))
        XCTAssertTrue(view.activeTrackingAreaOptions.contains(.mouseMoved))
        XCTAssertTrue(view.activeTrackingAreaOptions.contains(.mouseEnteredAndExited))
        XCTAssertTrue(view.activeTrackingAreaOptions.contains(.enabledDuringMouseDrag))
    }

    func testMovingIntoWindowEnablesMouseMovedEvents() {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 320, height: 200),
            styleMask: [.titled, .closable, .resizable],
            backing: .buffered,
            defer: false
        )
        let container = NSView(frame: window.contentView?.bounds ?? .zero)
        let view = HeaderDividerCursorOverlayNSView(
            frame: NSRect(x: 0, y: 0, width: 260, height: 24)
        )

        window.acceptsMouseMovedEvents = false
        window.contentView = container
        container.addSubview(view)

        XCTAssertTrue(window.acceptsMouseMovedEvents)
    }

    func testMouseMoveOverDividerSwitchesCurrentCursorToResize() throws {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 320, height: 200),
            styleMask: [.titled, .closable, .resizable],
            backing: .buffered,
            defer: false
        )
        let container = NSView(frame: window.contentView?.bounds ?? .zero)
        let view = HeaderDividerCursorOverlayNSView(
            frame: NSRect(x: 0, y: 0, width: 260, height: 24)
        )

        window.contentView = container
        container.addSubview(view)
        view.renderedColumnWidths = [80, 90, 100]
        view.updateTrackingAreas()

        NSCursor.arrow.set()
        view.mouseMoved(with: try XCTUnwrap(mouseMovedEvent(window: window, point: NSPoint(x: 80, y: 12))))

        XCTAssertEqual(NSCursor.current, NSCursor.resizeLeftRight)
    }

    func testMouseMoveAwayFromDividerRestoresArrowCursor() throws {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 320, height: 200),
            styleMask: [.titled, .closable, .resizable],
            backing: .buffered,
            defer: false
        )
        let container = NSView(frame: window.contentView?.bounds ?? .zero)
        let view = HeaderDividerCursorOverlayNSView(
            frame: NSRect(x: 0, y: 0, width: 260, height: 24)
        )

        window.contentView = container
        container.addSubview(view)
        view.renderedColumnWidths = [80, 90, 100]
        view.updateTrackingAreas()

        NSCursor.resizeLeftRight.set()
        view.mouseMoved(with: try XCTUnwrap(mouseMovedEvent(window: window, point: NSPoint(x: 20, y: 12))))

        XCTAssertEqual(NSCursor.current, NSCursor.arrow)
    }

    private func mouseMovedEvent(window: NSWindow, point: NSPoint) -> NSEvent? {
        NSEvent.mouseEvent(
            with: .mouseMoved,
            location: point,
            modifierFlags: [],
            timestamp: ProcessInfo.processInfo.systemUptime,
            windowNumber: window.windowNumber,
            context: nil,
            eventNumber: 0,
            clickCount: 0,
            pressure: 0
        )
    }
}
