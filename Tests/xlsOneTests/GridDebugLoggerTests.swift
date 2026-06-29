import XCTest
@testable import xlsOneUI

final class GridDebugLoggerTests: XCTestCase {
    func testLoggerIsDisabledByDefaultDuringTests() {
        XCTAssertFalse(GridDebugLogger.isEnabled(environment: ["XCTestConfigurationFilePath": "/tmp/test.xctest"]))
    }

    func testLoggerCanBeExplicitlyEnabledForDebugging() {
        XCTAssertTrue(
            GridDebugLogger.isEnabled(
                environment: [
                    "XCTestConfigurationFilePath": "/tmp/test.xctest",
                    "XLSONE_CURSOR_DEBUG_LOG": "1"
                ]
            )
        )
    }
}
