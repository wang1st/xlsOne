import SwiftUI

struct ColumnResizeController {
    private(set) var dragStartWidth: CGFloat = 0
    private(set) var draggingColumn: Int? = nil

    let minimumWidth: CGFloat

    init(minimumWidth: CGFloat = GridMetrics.minimumColumnWidth) {
        self.minimumWidth = minimumWidth
    }

    mutating func beginResize(column: Int, width: CGFloat) {
        draggingColumn = column
        dragStartWidth = width
    }

    func updatedWidth(translation: CGFloat) -> CGFloat {
        GridMetrics.clampedWidth(
            start: dragStartWidth,
            translation: translation,
            minimum: minimumWidth
        )
    }

    mutating func endResize() {
        draggingColumn = nil
        dragStartWidth = 0
    }
}

struct ColumnResizeHandle: View {
    let isDragging: Bool
    let height: CGFloat

    var body: some View {
        Rectangle()
            .fill(Color.gray.opacity(isDragging ? 0.45 : 0.2))
            .frame(width: 1, height: max(0, height - 4))
            .padding(.vertical, 2)
            .offset(x: 0.5)
            .zIndex(1)
    }
}
