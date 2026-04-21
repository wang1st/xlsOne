import SwiftUI
import XCTest
import xlsOneCore
@testable import xlsOne

@MainActor
final class ExcelGridLayoutTests: XCTestCase {
    func testHeaderAndFirstBodyRowShareSameColumnWidth() throws {
        let recorder = GridFrameRecorder(
            expectation: expectation(description: "header and body frames"),
            required: [
                .header(0),
                .rowHeader(0),
                .body(CellPosition(row: 0, col: 0))
            ]
        )

        let host = hostGrid(
            initialColumnWidths: [0: 120, 1: 88],
            recorder: recorder
        )

        wait(for: [recorder.expectation], timeout: 2.0)
        settleLayout(for: host)

        let header = try XCTUnwrap(recorder.frames[.header(0)])
        let rowHeader = try XCTUnwrap(recorder.frames[.rowHeader(0)])
        let body = try XCTUnwrap(recorder.frames[.body(CellPosition(row: 0, col: 0))])

        XCTAssertEqual(header.width, body.width, accuracy: 0.5, "\(recorder.frames)")
        XCTAssertEqual(header.minX, body.minX, accuracy: 0.5, "\(recorder.frames)")
        XCTAssertEqual(rowHeader.minY, body.minY, accuracy: 0.5, "\(recorder.frames)")
        XCTAssertEqual(rowHeader.maxX, body.minX, accuracy: 0.5, "\(recorder.frames)")
    }

    func testManualWidthOverrideAlignsHeaderAndBody() throws {
        let recorder = GridFrameRecorder(
            expectation: expectation(description: "manual width frames"),
            required: [
                .header(1),
                .body(CellPosition(row: 0, col: 1))
            ]
        )

        let host = hostGrid(
            initialColumnWidths: [0: 96, 1: 150],
            recorder: recorder
        )

        wait(for: [recorder.expectation], timeout: 2.0)
        settleLayout(for: host)

        let header = try XCTUnwrap(recorder.frames[.header(1)])
        let body = try XCTUnwrap(recorder.frames[.body(CellPosition(row: 0, col: 1))])

        XCTAssertEqual(
            header.width,
            GridMetrics.renderedWidth(forContentWidth: 150),
            accuracy: 0.5,
            "\(recorder.frames)"
        )
        XCTAssertEqual(header.width, body.width, accuracy: 0.5, "\(recorder.frames)")
        XCTAssertEqual(header.minX, body.minX, accuracy: 0.5, "\(recorder.frames)")
    }

    private func hostGrid(
        initialColumnWidths: [Int: CGFloat],
        recorder: GridFrameRecorder
    ) -> NSHostingView<AnyView> {
        let rootView = AnyView(
            ExcelGridView(
            rows: sampleRows,
            initialColumnWidths: initialColumnWidths,
            layoutObserver: GridLayoutObserver { frames in
                recorder.record(frames)
            }
        )
        .frame(width: 520, height: 240)
        )

        let host = NSHostingView(rootView: rootView)
        host.frame = NSRect(x: 0, y: 0, width: 520, height: 240)
        host.layoutSubtreeIfNeeded()
        RunLoop.main.run(until: Date().addingTimeInterval(0.1))
        return host
    }

    private var sampleRows: [[MergedCell]] {
        [
            [cell("科目编码"), cell("科目名称"), cell("金额")],
            [cell("201"), cell("一般公共服务支出"), cell("1250.50")],
            [cell("301"), cell("教育支出"), cell("4310.75")]
        ]
    }

    private func cell(_ value: String) -> MergedCell {
        MergedCell(type: .single(value))
    }

    private func settleLayout(for host: NSHostingView<AnyView>) {
        host.layoutSubtreeIfNeeded()
        RunLoop.main.run(until: Date().addingTimeInterval(0.2))
    }
}

@MainActor
private final class GridFrameRecorder {
    let expectation: XCTestExpectation
    let required: Set<GridFrameProbe>

    private(set) var frames: [GridFrameProbe: CGRect] = [:]
    private var didFulfill = false

    init(expectation: XCTestExpectation, required: Set<GridFrameProbe>) {
        self.expectation = expectation
        self.required = required
    }

    func record(_ newFrames: [GridFrameProbe: CGRect]) {
        frames.merge(newFrames) { _, new in new }

        guard !didFulfill else { return }
        guard required.allSatisfy({ frames[$0] != nil }) else { return }

        didFulfill = true
        expectation.fulfill()
    }
}
