import AppKit
import XCTest
@testable import xlsOne

final class HeaderDividerInteractionModelTests: XCTestCase {
    func testDividerIndexUsesExpandedHitBandAroundEachBoundary() {
        let model = HeaderDividerInteractionModel(
            renderedColumnWidths: [120, 90, 80],
            hitWidth: 72
        )

        XCTAssertEqual(model.dividerIndex(at: 120), 0)
        XCTAssertEqual(model.dividerIndex(at: 84), 0)
        XCTAssertEqual(model.dividerIndex(at: 156), 0)
        XCTAssertEqual(model.dividerIndex(at: 210), 1)
        XCTAssertEqual(model.dividerIndex(at: 174), 1)
        XCTAssertEqual(model.dividerIndex(at: 246), 1)
    }

    func testDividerIndexReturnsNilOutsideDividerBand() {
        let model = HeaderDividerInteractionModel(
            renderedColumnWidths: [120, 90, 80],
            hitWidth: 72
        )

        XCTAssertNil(model.dividerIndex(at: 20))
        XCTAssertNil(model.dividerIndex(at: 165))
        // 285 is within the last divider band at 290 ± 36 → [254, 326]
        XCTAssertEqual(model.dividerIndex(at: 285), 2)
    }

    func testCursorRectsClampToVisibleBounds() {
        let model = HeaderDividerInteractionModel(
            renderedColumnWidths: [80, 90, 100],
            hitWidth: 72
        )

        let rects = model.cursorRects(in: NSRect(x: 0, y: 0, width: 210, height: 24))

        XCTAssertEqual(rects.count, 2)
        XCTAssertEqual(rects[0], NSRect(x: 44, y: 0, width: 72, height: 24))
        XCTAssertEqual(rects[1], NSRect(x: 134, y: 0, width: 72, height: 24))
    }
}
