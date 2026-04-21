import SwiftUI

struct GridHeaderCell: View {
    let title: String
    let contentWidth: CGFloat
    let probe: GridFrameProbe?

    var body: some View {
        GridColumnFrame(
            contentWidth: contentWidth,
            height: GridMetrics.headerHeight,
            alignment: .center,
            probe: probe
        ) {
            Text(title)
                .font(.system(size: 11))
                .fontWeight(.medium)
                .foregroundStyle(.secondary)
                .lineLimit(1)
                .truncationMode(.tail)
        }
        .background(Color(NSColor.controlBackgroundColor))
        .overlay(
            Rectangle()
                .stroke(Color.gray.opacity(0.4), lineWidth: 0.5)
        )
    }
}
