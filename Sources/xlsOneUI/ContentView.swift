import SwiftUI
import xlsOneCore
import xlsOneLicense

struct ContentView: View {
    @EnvironmentObject var viewModel: AppViewModel
    @EnvironmentObject var licenseManager: LicenseManager
    @ObservedObject private var localeManager = LocaleManager.shared
    @State private var isDropTargeted = false

    var body: some View {
        VStack(spacing: 0) {
            if !viewModel.selectedFilePaths.isEmpty {
                WorkspaceToolbar()
                Divider()
            }

            switch viewModel.workspacePhase {
            case .idle:
                emptyView
            case .validating:
                loadingView
            case .blocked:
                blockedView
            case .ready:
                readyWorkspace
            }
        }
        .id(localeManager.currentLanguage.rawValue)
        .onChange(of: viewModel.showError) { isPresented in
            guard isPresented else { return }
            showErrorAlert()
        }
        .background {
            CenteredDialogWindow(
                isPresented: $viewModel.showSchemaManager,
                titleKey: "当前修正规则",
                size: NSSize(width: 600, height: 400)
            ) {
                SchemaManagerView()
                    .id(localeManager.currentLanguage.rawValue)
            }
            .frame(width: 0, height: 0)
        }
        .background {
            CenteredDialogWindow(
                isPresented: licenseActivationSheet,
                titleKey: "激活 表表归一",
                size: NSSize(width: 720, height: 520)
            ) {
                LicenseActivationView()
                    .id(localeManager.currentLanguage.rawValue)
            }
            .frame(width: 0, height: 0)
        }
        .background {
            CenteredDialogWindow(
                isPresented: $viewModel.showHelpPanel,
                titleKey: "快速参考指南",
                size: NSSize(width: 1000, height: 700)
            ) {
                HelpView()
                    .id(localeManager.currentLanguage.rawValue)
            }
            .frame(width: 0, height: 0)
        }
        .onDrop(of: [.fileURL], delegate: DropDelegateView(viewModel: viewModel, isTargeted: $isDropTargeted))
        .task {
            await licenseManager.verifyOnLaunch()
        }
    }

    private var licenseActivationSheet: Binding<Bool> {
        Binding(
            get: {
                if LicenseManager.isAppStoreDistribution { return false }
                let state = licenseManager.licenseState
                if state == .activated { return false }
                if licenseManager.showActivationSheet {
                    return true
                }
                if case .trial = state { return false }
                return state == .unactivated || state == .expired
            },
            set: { newValue in
                if !newValue {
                    licenseManager.showActivationSheet = false
                }
            }
        )
    }

    private func showErrorAlert() {
        WorkspaceDialogPresenter.runAlert(
            title: LocaleManager.loc("错误"),
            message: viewModel.errorMessage ?? LocaleManager.loc("未知错误"),
            style: .warning
        )
        viewModel.showError = false
    }


    private var emptyView: some View {
        WorkspaceEmptyState(isDropTargeted: isDropTargeted)
    }

    private var loadingView: some View {
        WorkspaceLoadingView(fileCount: viewModel.selectedFilePaths.count)
    }

    private var blockedView: some View {
        Group {
            if let report = viewModel.validationReport {
                WorkspaceBlockedView(report: report)
            } else {
                VStack(spacing: 12) {
                    Image(systemName: "exclamationmark.triangle")
                        .font(.largeTitle)
                        .foregroundColor(XColor.warning)
                    Text(LocaleManager.loc("无法加载校验报告"))
                        .foregroundColor(XColor.secondaryLabel)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(XColor.background)
            }
        }
    }

    private var readyWorkspace: some View {
        VStack(spacing: 0) {
            if let report = viewModel.validationReport {
                SheetCapsuleStrip(
                    items: viewModel.sheetOverviewItems,
                    report: report,
                    selection: viewModel.selectedSheetSelection
                ) { item in
                    switch item.status {
                    case .mergeable:
                        viewModel.switchToSheet(item.sheetName)
                    case .skipped:
                        viewModel.switchToSkippedSheet(item.sheetName)
                    }
                }
                Divider()
            }

            if let consensus = viewModel.selectedSkippedSheetConsensus {
                skippedSheetWorkspace(consensus: consensus)
            } else if let result = viewModel.mergedResult {
                mergeableSheetWorkspace(result: result)
            } else {
                VStack(spacing: 12) {
                    Image(systemName: "tablecells")
                        .font(.largeTitle)
                        .foregroundStyle(.secondary)
                    Text(LocaleManager.loc("当前没有可显示的汇总结果"))
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .background(Color(NSColor.controlBackgroundColor))
            }

            if viewModel.correctionCount > 0 {
                Divider()
                correctionBar
            }
        }
    }

    private func mergeableSheetWorkspace(result: MergedResult) -> some View {
        HStack(spacing: 0) {
            ExcelGridView(
                rows: result.rows,
                selectedCell: Binding(
                    get: { viewModel.selectedCell },
                    set: { viewModel.selectCell($0) }
                ),
                anomalyPositions: Set(viewModel.anomalyQueue.map(\.position)),
                manualOverridePositions: viewModel.manualOverridePositionsForCurrentSheet,
                onApplyOverride: { row, col, type in
                    viewModel.applyCellOverride(row: row, col: col, type: type)
                },
                onApplyBulkOverride: { positions, type in
                    viewModel.applyBulkOverride(positions: positions, type: type)
                },
                onJumpNextAnomaly: {
                    viewModel.jumpToNextAnomaly()
                },
                onJumpPreviousAnomaly: {
                    viewModel.jumpToPreviousAnomaly()
                }
            )

            Divider()

            InspectionSidebar()
                .environmentObject(viewModel)
                .frame(width: 360)
        }
    }

    private func skippedSheetWorkspace(consensus: SkippedSheetConsensus) -> some View {
        SkippedSheetWorkspaceView(consensus: consensus)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Color(NSColor.controlBackgroundColor))
    }

    private var correctionBar: some View {
        HStack {
            Text(String(format: LocaleManager.loc("已调整 %lld 处"), viewModel.correctionCount))
                .font(.caption)
                .foregroundStyle(.secondary)

            Spacer()

            Button(LocaleManager.loc("撤销上一步")) {
                viewModel.undoLastOverride()
            }
            .font(.caption)
            .buttonStyle(.bordered)
            .controlSize(.small)

            Button(LocaleManager.loc("清除本批次调整")) {
                viewModel.clearOverrides()
            }
            .font(.caption)
            .buttonStyle(.bordered)
            .controlSize(.small)
        }
        .padding(.horizontal)
        .padding(.vertical, 8)
        .background(Color(NSColor.controlBackgroundColor))
    }
}

private extension FileValidationReport {
    var statusColor: Color {
        switch status {
        case .included: return .green
        case .warning: return .orange
        case .blocked: return .red
        }
    }

    var statusLabel: String {
        switch status {
        case .included: return LocaleManager.loc("参与合并")
        case .warning: return LocaleManager.loc("已跳过")
        case .blocked: return LocaleManager.loc("阻断")
        }
    }
}

struct SheetCapsuleStrip: View {
    let items: [SheetOverviewItem]
    let report: WorkbookValidationReport
    let selection: WorkspaceSheetSelection?
    let onSelectSheet: (SheetOverviewItem) -> Void

    var body: some View {
        HorizontalWheelScrollView {
            HStack(spacing: 6) {
                ForEach(items) { item in
                    capsuleButton(for: item)
                }
            }
            .padding(.horizontal)
            .padding(.vertical, 8)
        }
        .frame(height: 48)
        .background(Color(NSColor.windowBackgroundColor))
    }

    private func capsuleButton(for item: SheetOverviewItem) -> some View {
        Button {
            onSelectSheet(item)
        } label: {
            HStack(spacing: 6) {
                if item.status == .skipped {
                    Circle()
                        .fill(Color.orange.opacity(isSelected(item) ? 0.9 : 0.7))
                        .frame(width: 5, height: 5)
                }
                Text(item.sheetName)
                    .font(.system(size: 13, weight: tabWeight(for: item)))
                    .foregroundStyle(tabForeground(for: item))
                    .lineLimit(1)
            }
            .padding(.horizontal, 14)
            .padding(.vertical, 7)
            .background(
                RoundedRectangle(cornerRadius: 10, style: .continuous)
                    .fill(tabBackground(for: item))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 10, style: .continuous)
                    .stroke(tabBorder(for: item), lineWidth: 1)
            )
            .overlay(alignment: .bottom) {
                RoundedRectangle(cornerRadius: 2, style: .continuous)
                    .fill(tabIndicator(for: item))
                    .frame(height: 2)
                    .padding(.horizontal, 10)
                    .padding(.bottom, 4)
            }
        }
        .buttonStyle(.plain)
        .help(tooltip(for: item))
    }

    private func tabWeight(for item: SheetOverviewItem) -> Font.Weight {
        if isSelected(item) {
            return .semibold
        }
        return .regular
    }

    private func tabForeground(for item: SheetOverviewItem) -> Color {
        switch item.status {
        case .mergeable:
            return isSelected(item) ? .accentColor : .primary
        case .skipped:
            return isSelected(item) ? .orange : .secondary
        }
    }

    private func tabBackground(for item: SheetOverviewItem) -> Color {
        switch item.status {
        case .mergeable:
            return isSelected(item) ? Color.white : Color(NSColor.controlBackgroundColor)
        case .skipped:
            return isSelected(item) ? Color.orange.opacity(0.08) : Color(NSColor.controlBackgroundColor)
        }
    }

    private func tabBorder(for item: SheetOverviewItem) -> Color {
        switch item.status {
        case .mergeable:
            return isSelected(item) ? Color.accentColor.opacity(0.22) : Color.secondary.opacity(0.10)
        case .skipped:
            return Color.orange.opacity(isSelected(item) ? 0.22 : 0.10)
        }
    }

    private func tabIndicator(for item: SheetOverviewItem) -> Color {
        guard isSelected(item) else { return .clear }
        switch item.status {
        case .mergeable:
            return Color.accentColor.opacity(0.85)
        case .skipped:
            return Color.orange.opacity(0.85)
        }
    }

    private func tooltip(for item: SheetOverviewItem) -> String {
        switch item.status {
        case .mergeable:
            return "\(item.sheetName)\n\(LocaleManager.loc("参与文件")): \(item.participatingFileCount)/\(item.totalFileCount)\n\(LocaleManager.loc("有效尺寸")): \(item.effectiveRowCount) x \(item.effectiveColumnCount)"
        case .skipped:
            if let consensus = WorkspaceDiagnostics.buildSkippedSheetConsensus(report: report, sheetName: item.sheetName) {
                return [
                    item.sheetName,
                    consensus.summary,
                    String(format: LocaleManager.loc("识别到 %lld 个结构分组"), consensus.groupCount)
                ].joined(separator: "\n")
            }
            let detail = item.detailMessages.joined(separator: "\n")
            return ([item.sheetName, item.reasonSummary, detail].compactMap { $0 }.joined(separator: "\n"))
        }
    }

    private func isSelected(_ item: SheetOverviewItem) -> Bool {
        switch (selection, item.status) {
        case (.mergeable(let sheetName), .mergeable):
            return sheetName == item.sheetName
        case (.skipped(let sheetName), .skipped):
            return sheetName == item.sheetName
        default:
            return false
        }
    }
}

struct SkippedSheetConsensusCard: View {
    let consensus: SkippedSheetConsensus

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 8) {
                Text(LocaleManager.loc("结构分组"))
                    .font(.subheadline)
                    .fontWeight(.semibold)
                Spacer()
                Text(String(format: LocaleManager.loc("%lld 个文件"), consensus.comparedFileCount))
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Text(String(format: LocaleManager.loc("%lld 组"), consensus.groupCount))
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            if let dominant = consensus.dominantGroupDescription {
                Text(LocaleManager.loc("主结构：") + dominant)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            VStack(alignment: .leading, spacing: 8) {
                ForEach(consensus.groups) { group in
                    VStack(alignment: .leading, spacing: 6) {
                        HStack(spacing: 8) {
                            Text(group.title)
                                .font(.caption)
                                .fontWeight(.semibold)
                            Text(String(format: LocaleManager.loc("%@ · %lld 个文件"), group.detail, group.fileCount))
                                .font(.caption)
                                .foregroundStyle(group.isDominant ? .primary : .secondary)
                            if group.isDominant {
                                Text(LocaleManager.loc("主"))
                                    .font(.caption2)
                                    .padding(.horizontal, 6)
                                    .padding(.vertical, 2)
                                    .background(Color.orange.opacity(0.12))
                                    .foregroundStyle(.orange)
                                    .clipShape(Capsule())
                            }
                            Spacer()
                        }

                        Text(group.filenames.joined(separator: "、"))
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .lineLimit(2)
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                    .padding(10)
                    .background(group.isDominant ? Color.orange.opacity(0.08) : Color(NSColor.controlBackgroundColor))
                    .clipShape(RoundedRectangle(cornerRadius: 10))
                }
            }
        }
        .padding(12)
        .background(Color(NSColor.windowBackgroundColor))
        .clipShape(RoundedRectangle(cornerRadius: 14))
        .overlay(
            RoundedRectangle(cornerRadius: 14)
                .stroke(Color.orange.opacity(0.18), lineWidth: 1)
        )
    }
}

struct SkippedSheetWorkspaceView: View {
    let consensus: SkippedSheetConsensus

    var body: some View {
        ScrollView {
            VStack(spacing: 24) {
                VStack(spacing: 10) {
                    Image(systemName: "square.stack.3d.up.slash")
                        .font(.system(size: 28, weight: .medium))
                        .foregroundStyle(.orange)

                    Text(consensus.sheetName)
                        .font(.title3)
                        .fontWeight(.semibold)

                    Text(LocaleManager.loc("未参与本次汇总"))
                        .font(.callout)
                        .fontWeight(.medium)
                        .foregroundStyle(.orange)

                    Text(consensus.summary)
                        .font(.callout)
                        .multilineTextAlignment(.center)
                        .foregroundStyle(.secondary)
                        .frame(maxWidth: 560)
                }
                .padding(.top, 28)

                SkippedSheetConsensusCard(consensus: consensus)
                    .frame(maxWidth: 860)

                Text(LocaleManager.loc("统一该 sheet 的有效行列范围后，可重新纳入合并。"))
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: 720)
                    .multilineTextAlignment(.center)
                    .padding(.bottom, 32)
            }
            .frame(maxWidth: .infinity)
            .padding(.horizontal, 24)
        }
    }
}

struct HorizontalWheelScrollView<Content: View>: NSViewRepresentable {
    let content: Content

    init(@ViewBuilder content: () -> Content) {
        self.content = content()
    }

    func makeNSView(context: Context) -> HorizontalWheelScrollContainer<Content> {
        HorizontalWheelScrollContainer(rootView: content)
    }

    func updateNSView(_ nsView: HorizontalWheelScrollContainer<Content>, context: Context) {
        nsView.update(rootView: content)
    }
}

final class HorizontalWheelScrollContainer<Content: View>: NSScrollView {
    private let hostingView: NSHostingView<Content>

    init(rootView: Content) {
        self.hostingView = NSHostingView(rootView: rootView)
        super.init(frame: .zero)
        drawsBackground = false
        borderType = .noBorder
        hasVerticalScroller = false
        hasHorizontalScroller = false
        autohidesScrollers = true
        scrollerStyle = .overlay
        documentView = hostingView
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func layout() {
        super.layout()
        updateDocumentFrame()
    }

    override func scrollWheel(with event: NSEvent) {
        let shouldMapVerticalDelta = abs(event.scrollingDeltaY) > abs(event.scrollingDeltaX)
        guard shouldMapVerticalDelta else {
            super.scrollWheel(with: event)
            return
        }

        let delta = event.hasPreciseScrollingDeltas ? event.scrollingDeltaY : event.scrollingDeltaY * 12
        scrollHorizontally(by: delta)
    }

    func update(rootView: Content) {
        hostingView.rootView = rootView
        hostingView.layoutSubtreeIfNeeded()
        updateDocumentFrame()
    }

    private func updateDocumentFrame() {
        let fittingSize = hostingView.fittingSize
        let viewportSize = contentView.bounds.size
        hostingView.frame = NSRect(
            x: 0,
            y: 0,
            width: max(fittingSize.width, viewportSize.width),
            height: max(fittingSize.height, viewportSize.height)
        )
    }

    private func scrollHorizontally(by delta: CGFloat) {
        guard let documentView else { return }
        let maxOffset = max(0, documentView.frame.width - contentView.bounds.width)
        var targetOrigin = contentView.bounds.origin
        targetOrigin.x = min(max(targetOrigin.x - delta, 0), maxOffset)
        contentView.setBoundsOrigin(targetOrigin)
        reflectScrolledClipView(contentView)
    }
}

struct GridScrollView<Content: View>: NSViewRepresentable {
    @Binding var scrollOffset: CGPoint
    let content: Content

    init(scrollOffset: Binding<CGPoint>, @ViewBuilder content: () -> Content) {
        self._scrollOffset = scrollOffset
        self.content = content()
    }

    func makeCoordinator() -> Coordinator {
        Coordinator(scrollOffset: $scrollOffset)
    }

    func makeNSView(context: Context) -> GridScrollContainer<Content> {
        let scrollView = GridScrollContainer(rootView: content)
        scrollView.onScroll = { offset in
            context.coordinator.update(offset)
        }
        return scrollView
    }

    func updateNSView(_ nsView: GridScrollContainer<Content>, context: Context) {
        context.coordinator.scrollOffset = $scrollOffset
        nsView.onScroll = { offset in
            context.coordinator.update(offset)
        }
        nsView.update(rootView: content)
    }

    final class Coordinator {
        var scrollOffset: Binding<CGPoint>

        init(scrollOffset: Binding<CGPoint>) {
            self.scrollOffset = scrollOffset
        }

        func update(_ offset: CGPoint) {
            guard scrollOffset.wrappedValue != offset else { return }
            scrollOffset.wrappedValue = offset
        }
    }
}

final class GridScrollContainer<Content: View>: NSScrollView {
    var onScroll: ((CGPoint) -> Void)?

    private let hostingView: NSHostingView<Content>
    private var boundsObserver: NSObjectProtocol?

    init(rootView: Content) {
        self.hostingView = NSHostingView(rootView: rootView)
        super.init(frame: .zero)
        drawsBackground = false
        borderType = .noBorder
        hasVerticalScroller = true
        hasHorizontalScroller = true
        autohidesScrollers = true
        scrollerStyle = .overlay
        contentView.postsBoundsChangedNotifications = true
        documentView = hostingView
        boundsObserver = NotificationCenter.default.addObserver(
            forName: NSView.boundsDidChangeNotification,
            object: contentView,
            queue: .main
        ) { [weak self] _ in
            self?.publishScrollOffset()
        }
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    deinit {
        if let boundsObserver {
            NotificationCenter.default.removeObserver(boundsObserver)
        }
    }

    override func layout() {
        super.layout()
        updateDocumentFrame()
        publishScrollOffset()
    }

    func update(rootView: Content) {
        hostingView.rootView = rootView
        hostingView.layoutSubtreeIfNeeded()
        updateDocumentFrame()
        publishScrollOffset()
    }

    private func updateDocumentFrame() {
        let fittingSize = hostingView.fittingSize
        let viewportSize = contentView.bounds.size
        hostingView.frame = NSRect(
            x: 0,
            y: 0,
            width: max(fittingSize.width, viewportSize.width),
            height: max(fittingSize.height, viewportSize.height)
        )
    }

    private func publishScrollOffset() {
        onScroll?(CGPoint(
            x: max(0, contentView.bounds.origin.x),
            y: max(0, contentView.bounds.origin.y)
        ))
    }
}

enum GridPinnedHeaderScrollSync {
    static func columnHeaderXOffset(for scrollOffset: CGPoint) -> CGFloat {
        -scrollOffset.x
    }

    static func rowHeaderYOffset(for scrollOffset: CGPoint) -> CGFloat {
        -scrollOffset.y
    }
}

struct ExcelGridView: View {
    let rows: [[MergedCell]]
    @Binding var selectedCell: CellPosition?
    let anomalyPositions: Set<CellPosition>
    let manualOverridePositions: Set<CellPosition>
    var initialColumnWidths: [Int: CGFloat] = [:]
    var layoutObserver: GridLayoutObserver? = nil
    var onApplyOverride: ((Int, Int, CellOverrideType) -> Void)?
    var onApplyBulkOverride: (([CellPosition], CellOverrideType) -> Void)?
    var onJumpNextAnomaly: (() -> Void)?
    var onJumpPreviousAnomaly: (() -> Void)?

    @State private var selectedCells: Set<CellPosition> = []
    @State private var lastSelectedCell: CellPosition?
    @State private var columnWidths: [Int: CGFloat] = [:]
    @State private var rowNumberColumnWidth: CGFloat = GridMetrics.rowNumberMinimumWidth
    @State private var columnResizeController = ColumnResizeController()
    @State private var scrollOffset: CGPoint = .zero

    init(
        rows: [[MergedCell]],
        selectedCell: Binding<CellPosition?> = .constant(nil),
        anomalyPositions: Set<CellPosition> = [],
        manualOverridePositions: Set<CellPosition> = [],
        initialColumnWidths: [Int: CGFloat] = [:],
        layoutObserver: GridLayoutObserver? = nil,
        onApplyOverride: ((Int, Int, CellOverrideType) -> Void)? = nil,
        onApplyBulkOverride: (([CellPosition], CellOverrideType) -> Void)? = nil,
        onJumpNextAnomaly: (() -> Void)? = nil,
        onJumpPreviousAnomaly: (() -> Void)? = nil
    ) {
        self.rows = rows
        self._selectedCell = selectedCell
        self.anomalyPositions = anomalyPositions
        self.manualOverridePositions = manualOverridePositions
        self.initialColumnWidths = initialColumnWidths
        self.layoutObserver = layoutObserver
        self.onApplyOverride = onApplyOverride
        self.onApplyBulkOverride = onApplyBulkOverride
        self.onJumpNextAnomaly = onJumpNextAnomaly
        self.onJumpPreviousAnomaly = onJumpPreviousAnomaly
    }

    private var maxCols: Int {
        rows.map { $0.count }.max() ?? 0
    }

    var body: some View {
        ZStack(alignment: .bottom) {
            GeometryReader { geometry in
                ZStack(alignment: .topLeading) {
                    GridScrollView(scrollOffset: $scrollOffset) {
                        HStack(alignment: .top, spacing: 0) {
                            VStack(alignment: .leading, spacing: 0) {
                                Color.clear
                                    .frame(height: GridMetrics.headerHeight)

                                ForEach(Array(rows.enumerated()), id: \.offset) { rowIdx, row in
                                    HStack(spacing: 0) {
                                        Color.clear.frame(width: rowNumberColumnWidth)

                                        HStack(spacing: 0) {
                                            ForEach(Array(row.enumerated()), id: \.offset) { colIdx, cell in
                                                let position = CellPosition(row: rowIdx, col: colIdx)
                                                cellView(for: cell, at: position, colIdx: colIdx)
                                            }
                                        }
                                    }
                                    .frame(height: GridMetrics.rowHeight)
                                }
                            }
                            Spacer(minLength: 0)
                        }
                        .frame(
                            minWidth: geometry.size.width,
                            minHeight: geometry.size.height,
                            alignment: .topLeading
                        )
                    }

                    topLeftCorner
                        .frame(width: rowNumberColumnWidth, height: GridMetrics.headerHeight)
                        .background(Color(NSColor.controlBackgroundColor))
                        .allowsHitTesting(false)

                    columnHeaders
                        .offset(x: GridPinnedHeaderScrollSync.columnHeaderXOffset(for: scrollOffset))
                        .frame(
                            width: max(0, geometry.size.width - rowNumberColumnWidth),
                            height: GridMetrics.headerHeight,
                            alignment: .leading
                        )
                        .clipped()
                        .background(Color(NSColor.controlBackgroundColor))
                        .overlay(alignment: .bottom) {
                            gridHorizontalSeparator
                        }
                        .offset(x: rowNumberColumnWidth)

                    rowNumbersColumn
                        .offset(y: GridPinnedHeaderScrollSync.rowHeaderYOffset(for: scrollOffset))
                        .frame(
                            width: rowNumberColumnWidth,
                            height: max(0, geometry.size.height - GridMetrics.headerHeight),
                            alignment: .top
                        )
                        .clipped()
                        .background(Color(NSColor.controlBackgroundColor))
                        .overlay(alignment: .top) {
                            gridHorizontalSeparator
                        }
                        .overlay(alignment: .trailing) {
                            gridVerticalSeparator
                        }
                        .offset(y: GridMetrics.headerHeight)
                        .allowsHitTesting(false)
                }
                .coordinateSpace(name: GridMetrics.coordinateSpaceName)
                .onPreferenceChange(GridFramePreferenceKey.self) { frames in
                    layoutObserver?.onFramesChange(frames)
                }
            }
            .onAppear {
                initializeColumnWidths()
                synchronizeExternalSelection()
            }
            .onChange(of: rows) { _ in
                initializeColumnWidths()
                synchronizeExternalSelection()
            }
            .onChange(of: selectedCell) { _ in
                synchronizeExternalSelection()
            }

            if selectedCells.count > 1 {
                SelectionToolbar(
                    selectedCount: selectedCells.count,
                    onApply: { type in
                        onApplyBulkOverride?(Array(selectedCells), type)
                        selectedCells = []
                    },
                    onCancel: {
                        selectedCells = []
                        synchronizeExternalSelection()
                    }
                )
                .padding(.bottom, 12)
                .transition(.move(edge: .bottom).combined(with: .opacity))
            }
        }
        .background(
            HotkeyMonitorView { key in
                handleHotkey(key)
            }
        )
    }

    private func synchronizeExternalSelection() {
        guard let selectedCell else { return }
        if selectedCells.count <= 1 {
            selectedCells = [selectedCell]
            lastSelectedCell = selectedCell
        }
    }

    private func handleHotkey(_ key: String) {
        switch key.lowercased() {
        case "1":
            applyQuickOverride(.label)
        case "2":
            applyQuickOverride(.sum)
        case "j":
            onJumpNextAnomaly?()
        case "k":
            onJumpPreviousAnomaly?()
        default:
            break
        }
    }

    private func applyQuickOverride(_ type: CellOverrideType) {
        if selectedCells.count > 1 {
            onApplyBulkOverride?(Array(selectedCells), type)
            selectedCells = []
        } else if let selectedCell {
            onApplyOverride?(selectedCell.row, selectedCell.col, type)
        }
    }

    private func cellView(for cell: MergedCell, at position: CellPosition, colIdx: Int) -> some View {
        ExcelCellView(
            cell: cell,
            isSelected: selectedCells.contains(position),
            isInSelectedRow: selectedCell?.row == position.row,
            isInSelectedColumn: selectedCell?.col == position.col,
            showsAnomalyMarker: anomalyPositions.contains(position),
            correctionState: correctionState(for: cell, at: position),
            width: contentWidth(for: colIdx),
            probe: layoutObserver.map { _ in .body(position) }
        )
        .contentShape(Rectangle())
        .onTapGesture {
            handleCellTap(position: position)
        }
        .contextMenu {
            Menu(LocaleManager.loc("修正为")) {
                Button(LocaleManager.loc("标签")) {
                    onApplyOverride?(position.row, position.col, .label)
                }
                Button(LocaleManager.loc("求和")) {
                    onApplyOverride?(position.row, position.col, .sum)
                }
            }
        }
    }

    private func initializeColumnWidths() {
        rowNumberColumnWidth = ColumnWidthCalculator.rowNumberWidth(totalRows: rows.count)
        let calculatedWidths = ColumnWidthCalculator.defaultWidths(for: rows)
        columnWidths = calculatedWidths.merging(initialColumnWidths) { _, override in override }
    }

    private func correctionState(for cell: MergedCell, at position: CellPosition) -> CellCorrectionState {
        if manualOverridePositions.contains(position) {
            return .manual
        }
        return cell.isOverridden ? .rule : .none
    }

    private func handleCellTap(position: CellPosition) {
        let isCommandPressed = NSApp.currentEvent?.modifierFlags.contains(.command) ?? false
        let isShiftPressed = NSApp.currentEvent?.modifierFlags.contains(.shift) ?? false

        if isShiftPressed, let lastSelectedCell {
            selectRange(from: lastSelectedCell, to: position)
        } else if isCommandPressed {
            if selectedCells.contains(position) {
                selectedCells.remove(position)
            } else {
                selectedCells.insert(position)
            }
            lastSelectedCell = position
            if selectedCells.count == 1 {
                selectedCell = position
            }
        } else {
            selectedCells = [position]
            selectedCell = position
            lastSelectedCell = position
        }
    }

    private func selectRange(from: CellPosition, to: CellPosition) {
        let minRow = min(from.row, to.row)
        let maxRow = max(from.row, to.row)
        let minCol = min(from.col, to.col)
        let maxCol = max(from.col, to.col)

        var range: Set<CellPosition> = []
        for row in minRow...maxRow {
            for col in minCol...maxCol {
                range.insert(CellPosition(row: row, col: col))
            }
        }
        selectedCells = range
        selectedCell = to
        lastSelectedCell = to
    }

    private var topLeftCorner: some View {
        Rectangle()
            .fill(Color(NSColor.controlBackgroundColor))
            .overlay(
                Rectangle()
                    .stroke(gridLineColor, lineWidth: GridMetrics.gridLineWidth)
            )
            .overlay(alignment: .bottom) {
                gridHorizontalSeparator
            }
            .overlay(alignment: .trailing) {
                gridVerticalSeparator
            }
    }

    private var rowNumbersColumn: some View {
        VStack(spacing: 0) {
            ForEach(0..<rows.count, id: \.self) { rowIdx in
                Text("\(rowIdx + 1)")
                    .font(.system(size: 11, weight: .medium))
                    .foregroundStyle(selectedCell?.row == rowIdx ? Color.accentColor : .secondary)
                    .frame(width: rowNumberColumnWidth, height: GridMetrics.rowHeight)
                    .background(selectedCell?.row == rowIdx ? Color.accentColor.opacity(0.08) : Color(NSColor.controlBackgroundColor))
                    .overlay(
                        Rectangle()
                            .stroke(gridLineColor, lineWidth: GridMetrics.gridLineWidth)
                    )
                    .overlay(rowHeaderProbe(rowIdx: rowIdx))
            }
        }
    }

    private var columnHeaders: some View {
        HStack(spacing: 0) {
            ForEach(0..<maxCols, id: \.self) { colIdx in
                ZStack(alignment: .trailing) {
                    GridHeaderCell(
                        title: WorkspaceDiagnostics.columnLetters(colIdx),
                        contentWidth: contentWidth(for: colIdx),
                        probe: layoutObserver.map { _ in .header(colIdx) }
                    )
                    .background(selectedCell?.col == colIdx ? Color.accentColor.opacity(0.08) : Color(NSColor.controlBackgroundColor))

                    ColumnResizeHandle(
                        isDragging: columnResizeController.draggingColumn == colIdx,
                        height: GridMetrics.headerHeight
                    )
                }
            }
        }
        .overlay(
            HeaderDividerCursorOverlay(
                renderedColumnWidths: renderedColumnWidths,
                onDragChanged: handleResizeChanged,
                onDragEnded: handleResizeEnded
            )
        )
    }

    private func contentWidth(for colIdx: Int) -> CGFloat {
        columnWidths[colIdx] ?? GridMetrics.defaultContentWidth
    }

    private var renderedColumnWidths: [CGFloat] {
        (0..<maxCols).map { colIdx in
            GridMetrics.renderedWidth(forContentWidth: contentWidth(for: colIdx))
        }
    }

    private func handleResizeChanged(_ colIdx: Int, _ translation: CGFloat) {
        if columnResizeController.draggingColumn == nil {
            columnResizeController.beginResize(column: colIdx, width: contentWidth(for: colIdx))
        }

        guard columnResizeController.draggingColumn == colIdx else { return }
        columnWidths[colIdx] = columnResizeController.updatedWidth(translation: translation)
    }

    private func handleResizeEnded(_ colIdx: Int) {
        guard columnResizeController.draggingColumn == colIdx else { return }
        columnResizeController.endResize()
    }

    private var gridLineColor: Color {
        Color.gray.opacity(0.35)
    }

    private var gridHorizontalSeparator: some View {
        Rectangle()
            .fill(gridLineColor)
            .frame(height: GridMetrics.gridLineWidth)
    }

    private var gridVerticalSeparator: some View {
        Rectangle()
            .fill(gridLineColor)
            .frame(width: GridMetrics.gridLineWidth)
    }

    @ViewBuilder
    private func rowHeaderProbe(rowIdx: Int) -> some View {
        if layoutObserver != nil {
            GeometryReader { proxy in
                Color.clear.preference(
                    key: GridFramePreferenceKey.self,
                    value: [.rowHeader(rowIdx): proxy.frame(in: .named(GridMetrics.coordinateSpaceName))]
                )
            }
        }
    }
}

struct SelectionToolbar: View {
    let selectedCount: Int
    let onApply: (CellOverrideType) -> Void
    let onCancel: () -> Void

    var body: some View {
        HStack(spacing: 12) {
            Text(String(format: LocaleManager.loc("已选 %lld 个"), selectedCount))
                .font(.caption)
                .foregroundStyle(.secondary)

            Divider()
                .frame(height: 16)

            Button(LocaleManager.loc("标签")) { onApply(.label) }
                .buttonStyle(.borderedProminent)
                .controlSize(.small)

            Button(LocaleManager.loc("求和")) { onApply(.sum) }
                .buttonStyle(.borderedProminent)
                .controlSize(.small)

            Divider()
                .frame(height: 16)

            Button {
                onCancel()
            } label: {
                Image(systemName: "xmark")
            }
            .buttonStyle(.borderless)
            .controlSize(.small)
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
        .background(
            RoundedRectangle(cornerRadius: 10)
                .fill(Color(NSColor.controlBackgroundColor))
                .shadow(color: .black.opacity(0.12), radius: 5, x: 0, y: 2)
        )
    }
}

struct HotkeyMonitorView: NSViewRepresentable {
    let onKeyDown: (String) -> Void

    func makeNSView(context: Context) -> NSView {
        let view = KeyMonitorView()
        view.onKeyDown = onKeyDown
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {
        guard let view = nsView as? KeyMonitorView else { return }
        view.onKeyDown = onKeyDown
    }
}

private class KeyMonitorView: NSView {
    var onKeyDown: ((String) -> Void)?
    private var monitor: Any?

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        if monitor == nil, window != nil {
            monitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
                guard let characters = event.charactersIgnoringModifiers else {
                    return event
                }

                if event.keyCode == 53 {
                    self?.onKeyDown?("escape")
                    return nil
                }

                self?.onKeyDown?(characters)
                return event
            }
        }
    }

    override func removeFromSuperview() {
        if let monitor {
            NSEvent.removeMonitor(monitor)
            self.monitor = nil
        }
        super.removeFromSuperview()
    }
}

struct ExcelCellView: View {
    let cell: MergedCell
    let isSelected: Bool
    let isInSelectedRow: Bool
    let isInSelectedColumn: Bool
    let showsAnomalyMarker: Bool
    let correctionState: CellCorrectionState
    let width: CGFloat
    let probe: GridFrameProbe?

    var body: some View {
        GridColumnFrame(
            contentWidth: width,
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
                .stroke(Color.gray.opacity(0.35), lineWidth: 0.5)
        )
        .overlay(alignment: .topTrailing) {
            statusMarker
        }
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
            return Color.accentColor.opacity(0.16)
        }
        if isInSelectedRow || isInSelectedColumn {
            return Color.accentColor.opacity(0.05)
        }
        switch cell.type {
        case .sum:
            return Color.blue.opacity(0.05)
        default:
            return .white
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

    @ViewBuilder
    private var statusMarker: some View {
        if let statusTint {
            Circle()
                .fill(statusTint)
                .frame(width: 5, height: 5)
                .padding(4)
        }
    }

    private var statusTint: Color? {
        if correctionState != .none {
            return CellStatusPalette.adjusted
        }
        return nil
    }
}

private enum CellStatusPalette {
    static let adjusted = Color(red: 0.16, green: 0.62, blue: 0.56)
}

struct InspectionSidebar: View {
    @EnvironmentObject var viewModel: AppViewModel
    @State private var isSourceListExpanded = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                if let cell = viewModel.selectedMergedCell,
                   let reference = viewModel.selectedCellReference {
                    let correctionState = viewModel.selectedCell.map {
                        viewModel.correctionState(for: $0, cell: cell)
                    } ?? .none
                    cellDetailCard(cell: cell, reference: reference, correctionState: correctionState)
                    sourceDisclosure(for: cell)
                } else {
                    placeholderCard(LocaleManager.loc("选择单元格后查看结果与来源。"))
                }
            }
            .padding()
        }
        .background(Color(NSColor.controlBackgroundColor))
    }

    private func cellDetailCard(
        cell: MergedCell,
        reference: String,
        correctionState: CellCorrectionState
    ) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(alignment: .top) {
                VStack(alignment: .leading, spacing: 6) {
                    Text(reference)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Text(cell.displayValue.isEmpty ? LocaleManager.loc("空值") : cell.displayValue)
                        .font(.system(size: 18, weight: .semibold, design: .monospaced))
                        .fixedSize(horizontal: false, vertical: true)
                }
                Spacer()
            }

            if let secondarySummary = secondarySummary(for: cell, correctionState: correctionState) {
                Text(secondarySummary)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(2)
            }

            Divider()

            VStack(alignment: .leading, spacing: 8) {
                HStack(spacing: 8) {
                    overrideButton(title: LocaleManager.loc("标签"), tint: .green, isActive: matches(cell.type, overrideType: .label)) {
                        applyOverride(.label)
                    }
                    overrideButton(title: LocaleManager.loc("求和"), tint: .blue, isActive: matches(cell.type, overrideType: .sum)) {
                        applyOverride(.sum)
                    }
                }

                if viewModel.canRestoreSelectedCellAutomatic {
                    Button(LocaleManager.loc("恢复自动判断")) {
                        viewModel.restoreAutomaticDecisionForSelectedCell()
                    }
                    .buttonStyle(.borderless)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                }

                Text(LocaleManager.loc("按 1 或 2"))
                    .font(.caption2)
                    .foregroundStyle(.tertiary)
            }
        }
        .padding()
        .background(Color.white)
        .clipShape(RoundedRectangle(cornerRadius: 12))
    }

    private func sourceDisclosure(for cell: MergedCell) -> some View {
        let overview = WorkspaceDiagnostics.buildSourceInspectionOverview(for: cell.sources)
        let compactNames = WorkspaceDiagnostics.compactSourceNames(for: cell.sources)

        return VStack(alignment: .leading, spacing: 10) {
            if !cell.sources.isEmpty {
                Button {
                    withAnimation(.easeInOut(duration: 0.16)) {
                        isSourceListExpanded.toggle()
                    }
                } label: {
                    HStack(alignment: .center, spacing: 12) {
                        Text(overview.summaryText)
                            .font(.caption)
                            .fontWeight(.medium)
                            .foregroundStyle(.secondary)
                        Spacer()
                        Image(systemName: isSourceListExpanded ? "chevron.up" : "chevron.down")
                            .font(.caption.weight(.semibold))
                            .foregroundStyle(.secondary)
                    }
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)

                if isSourceListExpanded {
                    Divider()

                    VStack(alignment: .leading, spacing: 0) {
                        ForEach(Array(cell.sources.enumerated()), id: \.offset) { index, source in
                            VStack(alignment: .leading, spacing: 6) {
                                HStack(alignment: .top, spacing: 10) {
                                    Text(compactNames[index])
                                        .font(.caption)
                                        .fontWeight(.medium)
                                        .lineLimit(1)
                                    Spacer()
                                    if let sourceStateText = sourceStateText(for: source.state) {
                                        Text(sourceStateText)
                                            .font(.caption2)
                                            .foregroundStyle(sourceStateColor(for: source.state))
                                    }
                                }

                                Text(sourceDisplayValue(source))
                                    .font(.system(.caption, design: .monospaced))
                                    .foregroundStyle(source.state == .value ? .primary : .secondary)
                                    .fixedSize(horizontal: false, vertical: true)
                            }
                            .padding(.vertical, 10)

                            if index < cell.sources.count - 1 {
                                Divider()
                            }
                        }
                    }
                }
            } else {
                placeholderCard(LocaleManager.loc("当前单元格没有可显示的来源。"))
            }
        }
        .padding()
        .background(Color.white)
        .clipShape(RoundedRectangle(cornerRadius: 12))
    }

    private func overrideButton(
        title: String,
        tint: Color,
        isActive: Bool,
        action: @escaping () -> Void
    ) -> some View {
        Button(title, action: action)
            .buttonStyle(InspectorOverrideButtonStyle(tint: tint, isActive: isActive))
            .controlSize(.small)
    }

    private func applyOverride(_ type: CellOverrideType) {
        guard let selectedCell = viewModel.selectedCell else { return }
        viewModel.applyCellOverride(row: selectedCell.row, col: selectedCell.col, type: type)
    }

    private func matches(_ cellType: MergedCell.CellType, overrideType: CellOverrideType) -> Bool {
        switch (cellType, overrideType) {
        case (.label, .label), (.sum, .sum):
            return true
        default:
            return false
        }
    }

    private func secondarySummary(
        for cell: MergedCell,
        correctionState: CellCorrectionState
    ) -> String? {
        if correctionState != .none {
            return LocaleManager.loc("当前按%@显示").replacingOccurrences(of: "%@", with: displayTypeName(for: cell.type))
        }
        return WorkspaceDiagnostics.decisionSummary(for: cell)
    }

    private func displayTypeName(for cellType: MergedCell.CellType) -> String {
        switch cellType {
        case .sum:
            return LocaleManager.loc("求和")
        default:
            return LocaleManager.loc("标签")
        }
    }

    private func sourceDisplayValue(_ source: CellSourceEntry) -> String {
        switch source.state {
        case .value:
            return source.value
        case .empty:
            return LocaleManager.loc("空值")
        case .missing:
            return LocaleManager.loc("缺失单元格")
        }
    }

    private func sourceStateText(for state: CellSourceState) -> String? {
        switch state {
        case .value:
            return nil
        case .empty:
            return LocaleManager.loc("空值")
        case .missing:
            return LocaleManager.loc("缺失")
        }
    }

    private func sourceStateColor(for state: CellSourceState) -> Color {
        switch state {
        case .value:
            return .secondary
        case .empty:
            return .secondary
        case .missing:
            return Color.secondary.opacity(0.7)
        }
    }

    private func placeholderCard(_ text: String) -> some View {
        Text(text)
            .font(.caption)
            .foregroundStyle(.secondary)
            .padding()
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Color.white)
            .clipShape(RoundedRectangle(cornerRadius: 12))
    }
}

private struct InspectorOverrideButtonStyle: ButtonStyle {
    let tint: Color
    let isActive: Bool

    func makeBody(configuration: Configuration) -> some View {
        InspectorOverrideButton(configuration: configuration, tint: tint, isActive: isActive)
    }
}

private struct InspectorOverrideButton: View {
    let configuration: InspectorOverrideButtonStyle.Configuration
    let tint: Color
    let isActive: Bool

    @Environment(\.isEnabled) private var isEnabled

    var body: some View {
        configuration.label
            .font(.system(size: 12, weight: .medium))
            .padding(.horizontal, 10)
            .padding(.vertical, 6)
            .foregroundStyle(foregroundColor)
            .background(
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .fill(backgroundColor)
            )
            .overlay(
                RoundedRectangle(cornerRadius: 8, style: .continuous)
                    .stroke(borderColor, lineWidth: 1)
            )
            .opacity(isEnabled ? 1 : 0.45)
    }

    private var foregroundColor: Color {
        guard isEnabled else { return .secondary }
        return isActive ? tint : .primary
    }

    private var backgroundColor: Color {
        guard isEnabled else { return .clear }
        if isActive {
            return tint.opacity(configuration.isPressed ? 0.18 : 0.14)
        }
        return configuration.isPressed ? Color.secondary.opacity(0.08) : .clear
    }

    private var borderColor: Color {
        if isActive {
            return tint.opacity(0.28)
        }
        return Color.secondary.opacity(0.12)
    }
}

/// A reusable dialog window that presents SwiftUI content inside an NSPanel.
/// Uses a localization key for the title so the window title stays in sync with the current language.
private struct CenteredDialogWindow<DialogContent: View>: NSViewRepresentable {
    @Binding var isPresented: Bool
    let titleKey: String
    let size: NSSize
    let content: () -> DialogContent

    init(
        isPresented: Binding<Bool>,
        titleKey: String,
        size: NSSize,
        @ViewBuilder content: @escaping () -> DialogContent
    ) {
        _isPresented = isPresented
        self.titleKey = titleKey
        self.size = size
        self.content = content
    }

    func makeNSView(context: Context) -> NSView {
        NSView(frame: .zero)
    }

    func updateNSView(_ nsView: NSView, context: Context) {
        context.coordinator.parent = self
        if isPresented {
            context.coordinator.present(anchor: nsView)
        } else {
            context.coordinator.close()
        }
    }

    func makeCoordinator() -> Coordinator {
        Coordinator(parent: self)
    }

    final class Coordinator: NSObject, NSWindowDelegate {
        var parent: CenteredDialogWindow
        private var panel: NSPanel?
        private var langObserver: NSObjectProtocol?

        init(parent: CenteredDialogWindow) {
            self.parent = parent
            super.init()
            langObserver = NotificationCenter.default.addObserver(
                forName: .appLanguageDidChange,
                object: nil,
                queue: .main
            ) { [weak self] _ in
                guard let self else { return }
                self.panel?.title = LocaleManager.loc(self.parent.titleKey)
            }
        }

        deinit {
            if let obs = langObserver {
                NotificationCenter.default.removeObserver(obs)
            }
        }

        func present(anchor: NSView) {
            let resolvedTitle = LocaleManager.loc(parent.titleKey)
            if let panel {
                // Panel exists but may be hidden/miniaturized — force it back to front.
                if panel.isVisible, !panel.isMiniaturized {
                    return // Already visible, skip redundant re-show
                }
                if panel.isMiniaturized { panel.deminiaturize(nil) }
                panel.title = resolvedTitle
                panel.setContentSize(parent.size)
                center(panel, anchor: anchor)
                panel.makeKeyAndOrderFront(nil)
                panel.orderFrontRegardless()
                NSApp.activate(ignoringOtherApps: true)
                return
            }

            let rootView = parent.content()
            let panel = NSPanel(
                contentRect: NSRect(origin: .zero, size: parent.size),
                styleMask: [.titled, .closable],
                backing: .buffered,
                defer: false
            )
            panel.title = resolvedTitle
            panel.isReleasedWhenClosed = false
            panel.delegate = self
            panel.contentViewController = NSHostingController(rootView: rootView)
            panel.setContentSize(parent.size)
            center(panel, anchor: anchor)
            panel.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
            self.panel = panel
        }

        func close() {
            guard let panel else { return }
            panel.delegate = nil
            panel.close()
            self.panel = nil
        }

        func windowWillClose(_ notification: Notification) {
            parent.isPresented = false
            panel = nil
        }

        private func center(_ panel: NSWindow, anchor: NSView) {
            guard let owner = anchor.window ?? NSApp.keyWindow ?? NSApp.mainWindow ?? NSApp.windows.first(where: { $0.isVisible }) else {
                panel.center()
                return
            }

            let ownerFrame = owner.frame
            let panelFrame = panel.frame
            panel.setFrameOrigin(NSPoint(
                x: ownerFrame.midX - panelFrame.width / 2,
                y: ownerFrame.midY - panelFrame.height / 2
            ))
        }
    }
}

struct DropDelegateView: DropDelegate {
    @ObservedObject var viewModel: AppViewModel
    @Binding var isTargeted: Bool

    func dropEntered(info: DropInfo) {
        isTargeted = validateDrop(info: info)
    }

    func dropExited(info: DropInfo) {
        isTargeted = false
    }

    func performDrop(info: DropInfo) -> Bool {
        let isValid = validateDrop(info: info)
        guard isValid else {
            isTargeted = false
            return false
        }

        let items = info.itemProviders(for: [.fileURL])
        isTargeted = false

        for item in items {
            item.loadItem(forTypeIdentifier: "public.file-url", options: nil) { data, _ in
                guard let data = data as? Data,
                      let url = URL(dataRepresentation: data, relativeTo: nil) else { return }

                let ext = url.pathExtension.lowercased()
                guard ext == "xlsx" || ext == "xls" else { return }

                DispatchQueue.main.async {
                    viewModel.loadFiles(at: [url.path], append: true)
                }
            }
        }

        return true
    }

    func dropUpdated(info: DropInfo) -> DropProposal? {
        DropProposal(operation: .copy)
    }

    func validateDrop(info: DropInfo) -> Bool {
        let isValid = info.hasItemsConforming(to: [.fileURL])
        if !isValid {
            isTargeted = false
        }
        return isValid
    }
}
