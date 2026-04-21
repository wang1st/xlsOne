import SwiftUI
import xlsOneCore

enum GridFrameProbe: Hashable {
    case header(Int)
    case rowHeader(Int)
    case body(CellPosition)
}

struct GridLayoutObserver {
    let onFramesChange: ([GridFrameProbe: CGRect]) -> Void

    init(_ onFramesChange: @escaping ([GridFrameProbe: CGRect]) -> Void) {
        self.onFramesChange = onFramesChange
    }
}

struct GridFramePreferenceKey: PreferenceKey {
    static var defaultValue: [GridFrameProbe: CGRect] = [:]

    static func reduce(
        value: inout [GridFrameProbe: CGRect],
        nextValue: () -> [GridFrameProbe: CGRect]
    ) {
        value.merge(nextValue()) { _, new in new }
    }
}

struct GridColumnFrame<Content: View>: View {
    let contentWidth: CGFloat
    let height: CGFloat
    let alignment: Alignment
    let probe: GridFrameProbe?

    @ViewBuilder private let content: Content

    init(
        contentWidth: CGFloat,
        height: CGFloat,
        alignment: Alignment = .leading,
        probe: GridFrameProbe? = nil,
        @ViewBuilder content: () -> Content
    ) {
        self.contentWidth = contentWidth
        self.height = height
        self.alignment = alignment
        self.probe = probe
        self.content = content()
    }

    var body: some View {
        content
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: alignment)
            .padding(.horizontal, GridMetrics.cellHorizontalInset)
            .frame(
                width: GridMetrics.renderedWidth(forContentWidth: contentWidth),
                height: height,
                alignment: alignment
            )
            .overlay(frameReporter)
    }

    @ViewBuilder
    private var frameReporter: some View {
        if let probe {
            GeometryReader { proxy in
                Color.clear
                    .preference(
                        key: GridFramePreferenceKey.self,
                        value: [probe: proxy.frame(in: .named(GridMetrics.coordinateSpaceName))]
                    )
            }
        }
    }
}
