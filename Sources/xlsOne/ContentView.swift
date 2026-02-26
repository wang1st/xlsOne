import SwiftUI
import UniformTypeIdentifiers
import AppKit

struct ContentView: View {
    @EnvironmentObject var appState: AppState
    
    @State private var columnWidths: [CGFloat] = []
    @State private var resizingColumn: Int?
    @State private var resizingStartWidth: CGFloat = 0
    @State private var isResizing: Bool = false
    @GestureState private var resizeDragDelta: CGFloat = 0
    
    private let rowHeaderWidth: CGFloat = 38
    private let colHeaderHeight: CGFloat = 24
    private let cellWidth: CGFloat = 92
    private let cellHeight: CGFloat = 24
    
    var body: some View {
        VStack(spacing: 0) {
            topToolbarView
            sheetTabsView
            Divider()
            HStack(spacing: 0) {
                canvasView
                Divider()
                inspectorPanelView
                    .frame(width: 280)
            }
            Divider()
            statusBarView
        }
        .background(Color(nsColor: .windowBackgroundColor))
        .onDrop(of: [.fileURL], isTargeted: nil) { providers in
            handleDrop(providers: providers)
            return true
        }
    }
    
    // MARK: - Top Toolbar (Numbers-like)
    
    private var topToolbarView: some View {
        HStack(spacing: 10) {
            Button(action: { appState.openFilePicker() }) {
                Label("打开", systemImage: "folder")
            }
            .buttonStyle(.bordered)
            
            Button(action: { Task { await appState.reload() } }) {
                Label("重载", systemImage: "arrow.clockwise")
            }
            .disabled(appState.loadedFiles.isEmpty)
            
            Button(action: { appState.reset() }) {
                Label("清空", systemImage: "trash")
            }
            .disabled(appState.loadedFiles.isEmpty)
            
            Spacer()
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(Color(nsColor: .windowBackgroundColor))
    }
    
    // MARK: - Sheet Tabs (Numbers-like)
    
    private var sheetTabsView: some View {
        HStack(spacing: 8) {
            Text("工作表")
                .font(.system(size: 12, weight: .semibold))
                .foregroundColor(.secondary)
            
            ScrollViewReader { proxy in
                MarqueeScrollView {
                    HStack(spacing: 6) {
                        ForEach(0..<appState.sheetNames.count, id: \.self) { index in
                            let isSelected = appState.selectedSheet == index
                            Text(appState.sheetNames[index])
                                .font(.system(size: 12, weight: .semibold))
                                .lineLimit(1)
                                .fixedSize(horizontal: true, vertical: false)
                                .padding(.horizontal, 10)
                                .padding(.vertical, 4)
                                .background(isSelected ? Color.accentColor.opacity(0.18) : Color.black.opacity(0.04))
                                .clipShape(Capsule())
                                .overlay(
                                    Capsule()
                                        .stroke(isSelected ? Color.accentColor.opacity(0.8) : Color.clear, lineWidth: 1)
                                )
                                .contentShape(Capsule())
                                .onTapGesture {
                                    appState.selectedSheet = index
                                    Task { await appState.refreshMergeForSelectedSheet() }
                                    withAnimation(.easeOut(duration: 0.2)) {
                                        proxy.scrollTo(index, anchor: .center)
                                    }
                                }
                                .id(index)
                        }
                    }
                    .padding(.horizontal, 8)
                }
                .frame(height: 30)
            }
            
            Spacer()
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 4)
        .background(Color(nsColor: .controlBackgroundColor))
    }
    
    // MARK: - Canvas
    
    private var canvasView: some View {
        ZStack {
            Color(nsColor: .windowBackgroundColor)
            if let mergedData = appState.mergedData, !mergedData.isEmpty {
                tableCanvasView(data: mergedData)
            } else {
                emptyResultsView
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
    
    private func tableCanvasView(data: [[MergedCell]]) -> some View {
        let cols = min(data.first?.count ?? 0, 50)
        let rows = min(data.count, 200)
        return ScrollView([.vertical, .horizontal]) {
            ZStack(alignment: .topLeading) {
                VStack(spacing: 0) {
                    tableHeaderRow(cols: cols)
                    HStack(spacing: 0) {
                        rowHeaderColumn(rows: rows)
                        tableBody(data: data, cols: cols, rows: rows)
                    }
                }
                
                resizeGuideLine(cols: cols, rows: rows)
            }
            .padding(.vertical, 16)
            .padding(.horizontal, 20)
        }
        .onAppear { ensureColumnWidths(count: cols) }
        .onChange(of: cols) { newValue in
            ensureColumnWidths(count: newValue)
        }
    }
    
    private func tableHeaderRow(cols: Int) -> some View {
        HStack(spacing: 0) {
            Rectangle()
                .fill(Color.clear)
                .frame(width: rowHeaderWidth, height: colHeaderHeight)
            ForEach(0..<cols, id: \.self) { col in
                Text(columnLetter(for: col))
                    .font(.system(size: 11, weight: .semibold))
                    .foregroundColor(.secondary)
                    .frame(width: columnWidth(for: col), height: colHeaderHeight)
                    .background(Color.black.opacity(0.03))
                    .overlay(
                        Rectangle()
                            .stroke(Color.black.opacity(0.06), lineWidth: 1)
                    )
                    .overlay(alignment: .trailing) {
                        Rectangle()
                            .fill(Color.clear)
                            .frame(width: 6)
                            .contentShape(Rectangle())
                            .onHover { isHovering in
                                if isHovering {
                                    NSCursor.resizeLeftRight.set()
                                } else {
                                    if !isResizing {
                                        NSCursor.arrow.set()
                                    }
                                }
                            }
                            .gesture(
                                DragGesture(minimumDistance: 0)
                                    .updating($resizeDragDelta) { value, state, _ in
                                        beginResizeColumn(col: col)
                                        state = value.translation.width
                                    }
                                    .onEnded { value in
                                        commitResizeColumn(col: col, delta: value.translation.width)
                                        endResizeColumn()
                                    }
                            )
                    }
            }
        }
        .animation(nil, value: columnWidths)
    }
    
    private func rowHeaderColumn(rows: Int) -> some View {
        VStack(spacing: 0) {
            ForEach(0..<rows, id: \.self) { row in
                Text("\(row + 1)")
                    .font(.system(size: 10, weight: .semibold))
                    .foregroundColor(.secondary)
                    .frame(width: rowHeaderWidth, height: cellHeight)
                    .background(Color.black.opacity(0.03))
                    .overlay(
                        Rectangle()
                            .stroke(Color.black.opacity(0.06), lineWidth: 1)
                    )
            }
        }
    }
    
    private func tableBody(data: [[MergedCell]], cols: Int, rows: Int) -> some View {
        VStack(spacing: 0) {
            ForEach(0..<rows, id: \.self) { row in
                HStack(spacing: 0) {
                    ForEach(0..<min(data[row].count, cols), id: \.self) { col in
                        cellView(
                            value: data[row][col].resultValue ?? "",
                            row: row,
                            col: col,
                            width: columnWidth(for: col)
                        )
                    }
                }
                .background(row % 2 == 0 ? Color.clear : Color.black.opacity(0.02))
            }
        }
        .animation(nil, value: columnWidths)
    }
    
    private var emptyResultsView: some View {
        VStack(spacing: 10) {
            Spacer()
            Image(systemName: "tablecells")
                .font(.system(size: 40))
                .foregroundColor(.secondary)
            Text("拖拽 Excel 文件开始")
                .font(.system(size: 13, weight: .semibold))
                .foregroundColor(.secondary)
            Text("或点击左上角的打开")
                .font(.system(size: 11))
                .foregroundColor(.secondary)
            Spacer()
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
    
    // MARK: - Inspector Panel (Numbers-like)
    
    private var inspectorPanelView: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("检视")
                    .font(.system(size: 12, weight: .semibold))
                    .foregroundColor(.secondary)
                Spacer()
                if let selected = appState.selectedCell {
                    Text(a1Notation(row: selected.row, column: selected.column))
                        .font(.system(size: 11, weight: .semibold))
                        .padding(.horizontal, 6)
                        .padding(.vertical, 3)
                        .background(Color.accentColor.opacity(0.12))
                        .clipShape(Capsule())
                }
            }
            .padding(.horizontal, 10)
            .padding(.top, 10)
            
            if let selected = appState.selectedCell {
                let baseline = baselineValue(row: selected.row, column: selected.column)
                Button(action: { copyInspectToPasteboard(row: selected.row, column: selected.column) }) {
                    Label("复制穿透值", systemImage: "doc.on.doc")
                        .font(.system(size: 11, weight: .semibold))
                }
                .buttonStyle(.bordered)
                .padding(.horizontal, 10)
                
                Divider()
                
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 6) {
                        ForEach(appState.loadedFiles) { file in
                            let value = valueFor(file: file, row: selected.row, column: selected.column)
                            let isDifferent = isDifferentValue(value, baseline: baseline)
                            HStack {
                                Text(file.filename)
                                    .font(.system(size: 12, weight: .semibold))
                                    .lineLimit(1)
                                    .truncationMode(.middle)
                                    .frame(width: 180, alignment: .leading)
                                Text(value.isEmpty ? "(空)" : value)
                                    .font(.system(size: 12, design: .monospaced))
                                    .lineLimit(1)
                                    .truncationMode(.middle)
                                    .foregroundColor(value.isEmpty ? .secondary : (isDifferent ? .accentColor : .primary))
                                Spacer()
                            }
                            .padding(.horizontal, 10)
                            .padding(.vertical, 2)
                            .background(isDifferent ? Color.accentColor.opacity(0.08) : Color.clear)
                            .clipShape(RoundedRectangle(cornerRadius: 6))
                        }
                    }
                    .padding(.bottom, 8)
                }
            } else {
                Text("选择单元格以查看穿透内容")
                    .font(.system(size: 12))
                    .foregroundColor(.secondary)
                    .padding(.horizontal, 10)
                    .padding(.vertical, 8)
                Spacer()
            }
        }
        .frame(maxHeight: .infinity)
        .background(Color(nsColor: .controlBackgroundColor))
    }
    
    // MARK: - Status Bar
    
    private var statusBarView: some View {
        HStack {
            Spacer()
            if let selected = appState.selectedCell {
                Text("选中：\(a1Notation(row: selected.row, column: selected.column))")
                    .font(.system(size: 12))
                    .foregroundColor(.secondary)
            }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(Color(nsColor: .windowBackgroundColor))
        .overlay(alignment: .top) {
            Rectangle()
                .fill(Color.black.opacity(0.05))
                .frame(height: 1)
        }
    }
    
    // MARK: - Helpers
    
    private func handleDrop(providers: [NSItemProvider]) {
        for provider in providers {
            provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier, options: nil) { item, _ in
                guard let data = item as? Data,
                      let url = URL(dataRepresentation: data, relativeTo: nil) else { return }
                
                let path = url.path
                if path.lowercased().hasSuffix(".xlsx") || path.lowercased().hasSuffix(".xls") {
                    Task { @MainActor in
                        await appState.addFiles(urls: [url])
                    }
                }
            }
        }
    }
    
    private func cellView(value: String, row: Int, col: Int, width: CGFloat) -> some View {
        let isSelected = appState.selectedCell?.row == row && appState.selectedCell?.column == col
        
        return ZStack(alignment: .leading) {
            Text(value)
                .font(.system(size: 12, design: .monospaced))
                .lineLimit(1)
                .truncationMode(.tail)
                .padding(.leading, 4)
        }
        .frame(width: width, height: cellHeight, alignment: .leading)
        .background(isSelected ? Color.accentColor.opacity(0.16) : Color.clear)
        .overlay(
            Rectangle()
                .stroke(isSelected ? Color.accentColor.opacity(0.8) : Color.black.opacity(0.05), lineWidth: 1)
        )
        .help(value)
        .onTapGesture {
            appState.selectedCell = CellPosition(row: row, column: col)
        }
    }
    
    private func valueFor(file: AppState.LoadedFile, row: Int, column: Int) -> String {
        let sheetName = appState.sheetNames.indices.contains(appState.selectedSheet)
            ? appState.sheetNames[appState.selectedSheet]
            : nil
        guard let sheetName = sheetName,
              let data = file.sheets[sheetName],
              row < data.count,
              column < data[row].count else {
            return ""
        }
        
        return data[row][column].rawValue ?? ""
    }
    
    private func baselineValue(row: Int, column: Int) -> String? {
        var counts: [String: Int] = [:]
        for file in appState.loadedFiles {
            let value = valueFor(file: file, row: row, column: column).trimmingCharacters(in: .whitespacesAndNewlines)
            if value.isEmpty { continue }
            counts[value, default: 0] += 1
        }
        return counts.max(by: { $0.value < $1.value })?.key
    }
    
    private func isDifferentValue(_ value: String, baseline: String?) -> Bool {
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let baseline = baseline, !trimmed.isEmpty else { return false }
        return trimmed != baseline
    }
    
    private func copyInspectToPasteboard(row: Int, column: Int) {
        var lines: [String] = []
        lines.append("穿透查阅: \(a1Notation(row: row, column: column))")
        for file in appState.loadedFiles {
            let value = valueFor(file: file, row: row, column: column)
            lines.append("\(file.filename)\t\(value.isEmpty ? "(空)" : value)")
        }
        let text = lines.joined(separator: "\n")
        let pasteboard = NSPasteboard.general
        pasteboard.clearContents()
        pasteboard.setString(text, forType: .string)
    }
    
    private func a1Notation(row: Int, column: Int) -> String {
        var col = column + 1
        var letters = ""
        while col > 0 {
            col -= 1
            let letter = Unicode.Scalar(UInt32(65 + col % 26))!
            letters = String(letter) + letters
            col /= 26
        }
        return "\(letters)\(row + 1)"
    }
    
    private func columnLetter(for index: Int) -> String {
        var col = index + 1
        var letters = ""
        while col > 0 {
            col -= 1
            let letter = Unicode.Scalar(UInt32(65 + col % 26))!
            letters = String(letter) + letters
            col /= 26
        }
        return letters
    }
    
    private func ensureColumnWidths(count: Int) {
        if columnWidths.count < count {
            columnWidths.append(contentsOf: Array(repeating: cellWidth, count: count - columnWidths.count))
        }
        if columnWidths.count > count {
            columnWidths = Array(columnWidths.prefix(count))
        }
    }
    
    private func columnWidth(for index: Int) -> CGFloat {
        if index < columnWidths.count {
            return columnWidths[index]
        }
        return cellWidth
    }
    
    private func beginResizeColumn(col: Int) {
        if resizingColumn != col {
            resizingColumn = col
            resizingStartWidth = columnWidth(for: col)
            if !isResizing {
                isResizing = true
                NSCursor.resizeLeftRight.push()
            }
        }
    }
    
    private func endResizeColumn() {
        resizingColumn = nil
        if isResizing {
            isResizing = false
            NSCursor.pop()
        }
    }
    
    private func commitResizeColumn(col: Int, delta: CGFloat) {
        guard col < columnWidths.count else { return }
        let baseWidth = resizingColumn == col ? resizingStartWidth : columnWidths[col]
        let newWidth = clampedWidth(baseWidth + delta)
        columnWidths[col] = newWidth
    }
    
    private func clampedWidth(_ width: CGFloat) -> CGFloat {
        let minWidth: CGFloat = 60
        let maxWidth: CGFloat = 240
        return min(max(width, minWidth), maxWidth)
    }
    
    private func resizeGuideLine(cols: Int, rows: Int) -> some View {
        guard let col = resizingColumn else {
            return AnyView(EmptyView())
        }
        let baseWidth = columnWidth(for: col)
        let previewWidth = clampedWidth(baseWidth + resizeDragDelta)
        let xOffset = rowHeaderWidth + columnWidths.prefix(col).reduce(0, +) + previewWidth
        let lineHeight = colHeaderHeight + CGFloat(rows) * cellHeight
        
        return AnyView(
            Rectangle()
                .fill(Color.accentColor.opacity(0.6))
                .frame(width: 1, height: lineHeight)
                .offset(x: xOffset, y: 0)
        )
    }
    
}

final class HorizontalWheelScrollView: NSScrollView {
    private let speedMultiplier: CGFloat = 2.4
    
    override func scrollWheel(with event: NSEvent) {
        let delta = abs(event.scrollingDeltaX) > abs(event.scrollingDeltaY)
            ? event.scrollingDeltaX
            : event.scrollingDeltaY
        if abs(delta) > 0 {
            let current = contentView.bounds.origin
            let newX = max(0, current.x - delta * speedMultiplier)
            contentView.scroll(to: NSPoint(x: newX, y: current.y))
            reflectScrolledClipView(contentView)
        } else {
            super.scrollWheel(with: event)
        }
    }
}

struct MarqueeScrollView<Content: View>: NSViewRepresentable {
    let content: Content
    
    init(@ViewBuilder content: () -> Content) {
        self.content = content()
    }
    
    func makeCoordinator() -> Coordinator {
        Coordinator()
    }
    
    func makeNSView(context: Context) -> NSScrollView {
        let scrollView = HorizontalWheelScrollView()
        scrollView.drawsBackground = false
        scrollView.hasVerticalScroller = false
        scrollView.hasHorizontalScroller = false
        scrollView.horizontalScrollElasticity = .allowed
        scrollView.verticalScrollElasticity = .none
        scrollView.autohidesScrollers = true
        scrollView.contentInsets = NSEdgeInsets(top: 0, left: 8, bottom: 0, right: 8)
        
        let container = NSView()
        let hostingView = NSHostingView(rootView: content)
        hostingView.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(hostingView)
        
        NSLayoutConstraint.activate([
            hostingView.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            hostingView.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            hostingView.topAnchor.constraint(equalTo: container.topAnchor),
            hostingView.bottomAnchor.constraint(equalTo: container.bottomAnchor)
        ])
        
        scrollView.documentView = container
        context.coordinator.container = container
        context.coordinator.hostingView = hostingView
        updateDocumentSize(for: context)
        
        return scrollView
    }
    
    func updateNSView(_ nsView: NSScrollView, context: Context) {
        if let hostingView = context.coordinator.hostingView {
            hostingView.rootView = content
            updateDocumentSize(for: context)
        }
    }
    
    private func updateDocumentSize(for context: Context) {
        guard let hostingView = context.coordinator.hostingView,
              let container = context.coordinator.container else { return }
        let size = hostingView.fittingSize
        container.frame = NSRect(origin: .zero, size: size)
        if let scrollView = container.enclosingScrollView {
            scrollView.contentView.scroll(to: .zero)
            scrollView.reflectScrolledClipView(scrollView.contentView)
        }
    }
    
    final class Coordinator {
        var container: NSView?
        var hostingView: NSHostingView<Content>?
    }
}
