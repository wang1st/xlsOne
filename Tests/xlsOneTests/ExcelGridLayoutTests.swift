import SwiftUI
import XCTest
import xlsOneCore
@testable import xlsOneUI

@MainActor
final class ExcelGridLayoutTests: XCTestCase {
    func testPinnedHeadersUseSpreadsheetScrollAxes() {
        let offset = CGPoint(x: 72, y: 56)

        XCTAssertEqual(GridPinnedHeaderScrollSync.columnHeaderXOffset(for: offset), -72)
        XCTAssertEqual(GridPinnedHeaderScrollSync.rowHeaderYOffset(for: offset), -56)
        XCTAssertEqual(GridPinnedHeaderScrollSync.columnHeaderXOffset(for: CGPoint(x: 0, y: 56)), 0)
        XCTAssertEqual(GridPinnedHeaderScrollSync.rowHeaderYOffset(for: CGPoint(x: 72, y: 0)), 0)
    }

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

    func testRowHeaderTracksBodyDuringVerticalScroll() throws {
        let trackedRow = 10
        let recorder = GridFrameRecorder(
            expectation: expectation(description: "scrolled row header and body frames"),
            required: [
                .header(0),
                .rowHeader(trackedRow),
                .body(CellPosition(row: trackedRow, col: 0))
            ]
        )

        let host = hostGrid(
            initialColumnWidths: [0: 120],
            recorder: recorder,
            rows: manyRows(count: 40),
            size: CGSize(width: 420, height: 160)
        )

        wait(for: [recorder.expectation], timeout: 2.0)
        settleLayout(for: host)

        let initialHeader = try XCTUnwrap(recorder.frames[.header(0)])
        let initialRowHeader = try XCTUnwrap(recorder.frames[.rowHeader(trackedRow)])
        let scrollView = try XCTUnwrap(host.firstDescendant(ofType: NSScrollView.self))

        scrollView.contentView.scroll(to: NSPoint(x: 0, y: GridMetrics.rowHeight * 4))
        scrollView.reflectScrolledClipView(scrollView.contentView)
        settleLayout(for: host)

        let scrolledHeader = try XCTUnwrap(recorder.frames[.header(0)])
        let scrolledRowHeader = try XCTUnwrap(recorder.frames[.rowHeader(trackedRow)])
        let scrolledBody = try XCTUnwrap(recorder.frames[.body(CellPosition(row: trackedRow, col: 0))])
        let visibleBodyMinY = scrolledBody.minY - scrollView.contentView.bounds.origin.y

        XCTAssertEqual(scrolledHeader.minY, initialHeader.minY, accuracy: 0.5, "\(recorder.frames)")
        XCTAssertTrue(
            scrolledRowHeader.minY < initialRowHeader.minY - GridMetrics.rowHeight,
            "\(recorder.frames)"
        )
        XCTAssertEqual(scrolledRowHeader.minY, visibleBodyMinY, accuracy: 1.0, "\(recorder.frames)")
    }

    private func hostGrid(
        initialColumnWidths: [Int: CGFloat],
        recorder: GridFrameRecorder,
        rows: [[MergedCell]]? = nil,
        size: CGSize = CGSize(width: 520, height: 240)
    ) -> NSHostingView<AnyView> {
        let rootView = AnyView(
            ExcelGridView(
            rows: rows ?? sampleRows,
            initialColumnWidths: initialColumnWidths,
            layoutObserver: GridLayoutObserver { frames in
                recorder.record(frames)
            }
        )
        .frame(width: size.width, height: size.height)
        )

        let host = NSHostingView(rootView: rootView)
        host.frame = NSRect(x: 0, y: 0, width: size.width, height: size.height)
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

    private func manyRows(count: Int) -> [[MergedCell]] {
        (0..<count).map { row in
            [cell("项目 \(row + 1)"), cell("\((row + 1) * 10)")]
        }
    }

    private func settleLayout(for host: NSHostingView<AnyView>) {
        host.layoutSubtreeIfNeeded()
        RunLoop.main.run(until: Date().addingTimeInterval(0.2))
    }
}

private extension NSView {
    func firstDescendant<T: NSView>(ofType type: T.Type) -> T? {
        if let view = self as? T {
            return view
        }
        for subview in subviews {
            if let match = subview.firstDescendant(ofType: type) {
                return match
            }
        }
        return nil
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
