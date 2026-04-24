import SwiftUI
import xlsOneCore

struct GridBodyCell: View {
    let cell: MergedCell
    let isSelected: Bool
    let contentWidth: CGFloat
    let probe: GridFrameProbe?

    var body: some View {
        GridColumnFrame(
            contentWidth: contentWidth,
            height: GridMetrics.rowHeight,
            alignment: alignment,
            probe: probe
        ) {
            Text(cell.displayValue)
                .font(.system(size: 12))
                .lineLimit(1)
                .truncationMode(.tail)
        }
        .background(backgroundColor)
        .foregroundStyle(foregroundColor)
        .overlay(
            Rectangle()
                .stroke(Color.gray.opacity(0.4), lineWidth: 0.5)
        )
        .overlay(
            Group {
                if isSelected {
                    Rectangle()
                        .stroke(Color.accentColor, lineWidth: 2)
                }
            }
        )
    }

    private var alignment: Alignment {
        switch cell.type {
        case .sum:
            return .trailing
        default:
            return .leading
        }
    }

    private var backgroundColor: Color {
        if isSelected {
            return Color.accentColor.opacity(0.15)
        }

        switch cell.type {
        case .sum:
            return Color.blue.opacity(0.05)
        default:
            return Color.white
        }
    }

    private var foregroundColor: Color {
        switch cell.type {
        case .sum:
            return .blue
        default:
            return .primary
        }
    }
}
