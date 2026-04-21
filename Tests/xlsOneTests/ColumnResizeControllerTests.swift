import XCTest
@testable import xlsOne

final class ColumnResizeControllerTests: XCTestCase {
    func testBeginResizeCapturesStartingWidth() {
        var controller = ColumnResizeController()

        controller.beginResize(column: 2, width: 120)

        XCTAssertEqual(controller.draggingColumn, 2)
        XCTAssertEqual(controller.dragStartWidth, 120)
    }

    func testUpdateResizeAppliesTranslationAndClamp() {
        var controller = ColumnResizeController()
        controller.beginResize(column: 2, width: 120)

        XCTAssertEqual(controller.updatedWidth(translation: 30), 150)
        XCTAssertEqual(controller.updatedWidth(translation: -200), 40)
    }
}
