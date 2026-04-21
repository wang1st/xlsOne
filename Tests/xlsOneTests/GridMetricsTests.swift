import XCTest
@testable import xlsOne

final class GridMetricsTests: XCTestCase {
    func testRenderedWidthMatchesContentWidthPlusInsets() {
        let metrics = GridMetrics(contentWidth: 96, horizontalInset: 4)

        XCTAssertEqual(metrics.renderedWidth, 104)
    }

    func testResizedWidthClampsToMinimum() {
        XCTAssertEqual(GridMetrics.clampedWidth(start: 80, translation: -100), 40)
    }
}
