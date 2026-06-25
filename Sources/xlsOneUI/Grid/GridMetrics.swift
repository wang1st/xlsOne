import AppKit
import Foundation
import xlsOneCore

struct GridMetrics {
    static let cellHorizontalInset: CGFloat = 4
    static let headerHeight: CGFloat = 24
    static let rowHeight: CGFloat = 24
    static let defaultRenderedWidth: CGFloat = 100
    static let minimumColumnWidth: CGFloat = 40
    static let minimumAutoWidth: CGFloat = 60
    static let maximumAutoWidth: CGFloat = 300
    static let resizeHandleHitWidth: CGFloat = 72
    static let gridLineWidth: CGFloat = 0.5
    static let rowNumberMinimumWidth: CGFloat = 40
    static let coordinateSpaceName = "gridContainer"

    let contentWidth: CGFloat
    let horizontalInset: CGFloat

    init(contentWidth: CGFloat, horizontalInset: CGFloat = GridMetrics.cellHorizontalInset) {
        self.contentWidth = contentWidth
        self.horizontalInset = horizontalInset
    }

    var renderedWidth: CGFloat {
        Self.renderedWidth(forContentWidth: contentWidth, horizontalInset: horizontalInset)
    }

    static var defaultContentWidth: CGFloat {
        defaultRenderedWidth - cellHorizontalInset * 2
    }

    static var minimumAutoContentWidth: CGFloat {
        minimumAutoWidth - cellHorizontalInset * 2
    }

    static var maximumAutoContentWidth: CGFloat {
        maximumAutoWidth - cellHorizontalInset * 2
    }

    static func renderedWidth(
        forContentWidth contentWidth: CGFloat,
        horizontalInset: CGFloat = GridMetrics.cellHorizontalInset
    ) -> CGFloat {
        max(0, contentWidth) + horizontalInset * 2
    }

    static func clampedWidth(
        start: CGFloat,
        translation: CGFloat,
        minimum: CGFloat = GridMetrics.minimumColumnWidth
    ) -> CGFloat {
        max(minimum, start + translation)
    }
}

enum ColumnWidthCalculator {
    static func defaultWidths(
        for rows: [[MergedCell]],
        font: NSFont = NSFont.systemFont(ofSize: 12)
    ) -> [Int: CGFloat] {
        let maxCols = rows.map(\.count).max() ?? 0
        guard maxCols > 0 else { return [:] }

        let attributes: [NSAttributedString.Key: Any] = [.font: font]
        let totalRows = rows.count
        var widths: [Int: CGFloat] = [:]

        for colIdx in 0..<maxCols {
            var widest = GridMetrics.minimumAutoContentWidth

            if let firstRow = rows.first, colIdx < firstRow.count {
                widest = max(
                    widest,
                    measuredContentWidth(firstRow[colIdx].displayValue, attributes: attributes)
                )
            }

            if totalRows > 1 {
                let step = max(1, totalRows / 50)
                var sampled = 0

                for rowIdx in stride(from: 1, to: totalRows, by: step) {
                    guard colIdx < rows[rowIdx].count else { continue }

                    widest = max(
                        widest,
                        measuredContentWidth(rows[rowIdx][colIdx].displayValue, attributes: attributes)
                    )

                    sampled += 1
                    if sampled >= 50 {
                        break
                    }
                }
            }

            widths[colIdx] = min(GridMetrics.maximumAutoContentWidth, widest)
        }

        return widths
    }

    static func rowNumberWidth(
        totalRows: Int,
        font: NSFont = NSFont.systemFont(ofSize: 11)
    ) -> CGFloat {
        let digits = max(1, String(totalRows).count)
        let rowNumberText = String(repeating: "8", count: digits)
        let width = (rowNumberText as NSString).size(withAttributes: [.font: font]).width
        return max(GridMetrics.rowNumberMinimumWidth, ceil(width) + 16)
    }

    private static func measuredContentWidth(
        _ text: String,
        attributes: [NSAttributedString.Key: Any]
    ) -> CGFloat {
        ceil((text as NSString).size(withAttributes: attributes).width)
    }
}
