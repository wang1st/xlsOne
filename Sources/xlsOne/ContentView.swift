import SwiftUI
import xlsOneCore

struct ContentView: View {
    @EnvironmentObject var viewModel: AppViewModel

    var body: some View {
        VStack(spacing: 0) {
            toolbar
            Divider()

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
        .alert("错误", isPresented: $viewModel.showError) {
            Button("确定", role: .cancel) {}
        } message: {
            Text(viewModel.errorMessage ?? "未知错误")
        }
        .sheet(isPresented: $viewModel.showSchemaManager) {
            SchemaManagerView()
        }
        .alert("保存 Schema", isPresented: $viewModel.showSaveSchemaDialog) {
            TextField("Schema 名称", text: $viewModel.newSchemaName)
            Button("取消", role: .cancel) {}
            Button("保存") {
                Task {
                    try? await viewModel.saveCurrentAsSchema(name: viewModel.newSchemaName)
                }
            }
        } message: {
            Text("将当前类型修正保存为后续可复用的 Schema")
        }
        .onDrop(of: [.fileURL], delegate: DropDelegateView(viewModel: viewModel))
    }

    private var toolbar: some View {
        let presentation = viewModel.toolbarPresentation

        return HStack(spacing: 12) {
            HStack(spacing: 8) {
                if presentation.importIsProminent {
                    Button {
                        viewModel.showOpenFileDialog()
                    } label: {
                        Label(presentation.importTitle, systemImage: "folder.badge.plus")
                    }
                    .buttonStyle(.borderedProminent)
                } else {
                    Button {
                        viewModel.showOpenFileDialog()
                    } label: {
                        Label(presentation.importTitle, systemImage: "folder.badge.plus")
                    }
                    .buttonStyle(.bordered)
                }

                Button {
                    viewModel.showAddFileDialog()
                } label: {
                    Label("追加文件", systemImage: "plus")
                }
                .buttonStyle(.bordered)
                .disabled(!presentation.appendEnabled)

            }

            Divider()
                .frame(height: 20)

            HStack(spacing: 8) {
                Button {
                    viewModel.showSchemaManagerWindow()
                } label: {
                    Label("规则库", systemImage: "books.vertical")
                }
                .buttonStyle(.bordered)

                Button {
                    viewModel.reloadFiles()
                } label: {
                    Label("重新校验", systemImage: "arrow.clockwise")
                }
                .buttonStyle(.bordered)
                .disabled(viewModel.selectedFilePaths.isEmpty)
            }

            Divider()
                .frame(height: 20)

            HStack(spacing: 8) {
                if presentation.exportIsProminent {
                    Button {
                        viewModel.exportResult()
                    } label: {
                        Label("导出汇总", systemImage: "square.and.arrow.up")
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(!viewModel.canExport)
                } else {
                    Button {
                        viewModel.exportResult()
                    } label: {
                        Label("导出汇总", systemImage: "square.and.arrow.up")
                    }
                    .buttonStyle(.bordered)
                    .disabled(!viewModel.canExport)
                }

                Button {
                    viewModel.closeAllFiles()
                } label: {
                    Label("新建批次", systemImage: "plus.circle")
                }
                .buttonStyle(.bordered)
                .disabled(viewModel.selectedFilePaths.isEmpty)
                .help("仅清空当前工作区，不影响原始 Excel 文件")
            }

            Spacer()

            HStack(spacing: 8) {
                statPill(title: "已选", value: "\(viewModel.selectedFilePaths.count)")
                statPill(title: "参与", value: "\(viewModel.participatingFileCount)")
                if viewModel.blockedFileCount > 0 {
                    statPill(title: "阻断", value: "\(viewModel.blockedFileCount)", tint: .red)
                }
                if viewModel.warningFileCount > 0 {
                    statPill(title: "警告", value: "\(viewModel.warningFileCount)", tint: .orange)
                }
                if let skippedSheetCount = viewModel.validationReport?.skippedSheetCount, skippedSheetCount > 0 {
                    statPill(title: "跳过sheet", value: "\(skippedSheetCount)", tint: .orange)
                }
            }
        }
        .padding()
        .background(Color(NSColor.windowBackgroundColor))
    }

    private func statPill(title: String, value: String, tint: Color = .secondary) -> some View {
        HStack(spacing: 4) {
            Text(title)
            Text(value)
                .fontWeight(.semibold)
        }
        .font(.caption)
        .padding(.horizontal, 8)
        .padding(.vertical, 5)
        .background(tint.opacity(0.12))
        .foregroundStyle(tint)
        .clipShape(Capsule())
    }

    private var emptyView: some View {
        VStack(spacing: 20) {
            Image(systemName: "tablecells.badge.ellipsis")
                .font(.system(size: 68))
                .foregroundStyle(.secondary)

            Text("快速合并同构 Excel")
                .font(.title2)
                .fontWeight(.semibold)

            Text("将多个结构一致的 Excel 汇总成同构结果文件。\n系统会先做结构校验，再进入可穿透、可修正的检查工作台。")
                .multilineTextAlignment(.center)
                .foregroundStyle(.secondary)

            HStack(spacing: 12) {
                Button(viewModel.toolbarPresentation.importTitle) {
                    viewModel.showOpenFileDialog()
                }
                .buttonStyle(.borderedProminent)
            }
            .padding(.top, 4)

            Text("也可以直接拖拽 Excel 文件到窗口。")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(NSColor.controlBackgroundColor))
    }

    private var loadingView: some View {
        VStack(spacing: 16) {
            ProgressView()
                .scaleEffect(1.4)
            Text("正在校验工作簿结构并准备汇总工作台…")
                .foregroundStyle(.secondary)
            Text("已选 \(viewModel.selectedFilePaths.count) 个文件")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(NSColor.controlBackgroundColor))
    }

    private var blockedView: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                HStack(alignment: .top, spacing: 12) {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .foregroundStyle(.orange)
                        .font(.title2)
                    VStack(alignment: .leading, spacing: 6) {
                        Text("没有可参与汇总的同构工作表")
                            .font(.title3)
                            .fontWeight(.semibold)
                        Text("系统已忽略尾部空白行列后重试校验，但当前仍没有任何 sheet 能在所有文件间对齐。")
                            .foregroundStyle(.secondary)
                    }
                }

                if let report = viewModel.validationReport {
                    validationSummary(report)
                    if report.skippedSheetCount > 0 {
                        skippedSheetList(report)
                    }
                    fileParticipationList(report.files)
                }
            }
            .padding(24)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .background(Color(NSColor.controlBackgroundColor))
    }

    private func validationSummary(_ report: WorkbookValidationReport) -> some View {
        HStack(spacing: 12) {
            statCard(title: "参与文件", value: "\(report.includedFiles.count)", tint: .green)
            statCard(title: "阻断文件", value: "\(report.blockedFiles.count)", tint: .red)
            statCard(title: "警告文件", value: "\(report.warningFiles.count)", tint: .orange)
            if report.skippedSheetCount > 0 {
                statCard(title: "跳过工作表", value: "\(report.skippedSheetCount)", tint: .orange)
            }
        }
    }

    private func statCard(title: String, value: String, tint: Color) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title)
                .font(.caption)
                .foregroundStyle(.secondary)
            Text(value)
                .font(.title3)
                .fontWeight(.semibold)
                .foregroundStyle(tint)
        }
        .padding()
        .frame(maxWidth: 140, alignment: .leading)
        .background(tint.opacity(0.1))
        .clipShape(RoundedRectangle(cornerRadius: 12))
    }

    private func fileParticipationList(_ reports: [FileValidationReport]) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("文件参与情况")
                .font(.headline)

            ForEach(reports, id: \.filepath) { report in
                VStack(alignment: .leading, spacing: 10) {
                    HStack(spacing: 10) {
                        statusDot(for: report.status)
                        Text(report.filename)
                            .fontWeight(.medium)
                        Spacer()
                        Text(report.statusLabel)
                            .font(.caption)
                            .foregroundStyle(report.statusColor)
                    }

                    if !report.issues.isEmpty {
                        VStack(alignment: .leading, spacing: 6) {
                            ForEach(report.issues) { issue in
                                Text(issue.message)
                                    .font(.caption)
                                    .foregroundStyle(issue.severity == .blocking ? .red : .orange)
                            }
                        }
                    }
                }
                .padding()
                .background(Color.white)
                .clipShape(RoundedRectangle(cornerRadius: 12))
                .overlay(
                    RoundedRectangle(cornerRadius: 12)
                        .stroke(report.statusColor.opacity(0.2), lineWidth: 1)
                )
            }
        }
    }

    private func skippedSheetList(_ report: WorkbookValidationReport) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("已跳过的工作表")
                .font(.headline)

            ForEach(report.skippedSheetNames, id: \.self) { sheetName in
                if let consensus = WorkspaceDiagnostics.buildSkippedSheetConsensus(report: report, sheetName: sheetName) {
                    SkippedSheetConsensusCard(consensus: consensus)
                } else {
                    VStack(alignment: .leading, spacing: 6) {
                        HStack {
                            Image(systemName: "exclamationmark.triangle.fill")
                                .foregroundStyle(.orange)
                            Text(sheetName)
                                .fontWeight(.medium)
                            Spacer()
                            Text("不参与合并")
                                .font(.caption)
                                .foregroundStyle(.orange)
                        }

                        ForEach(report.skippedSheetIssues.filter { $0.sheetName == sheetName }) { issue in
                            Text(issue.message)
                                .font(.caption)
                                .foregroundStyle(.orange)
                        }
                    }
                    .padding()
                    .background(Color.orange.opacity(0.08))
                    .clipShape(RoundedRectangle(cornerRadius: 12))
                }
            }
        }
    }

    private func statusDot(for status: FileValidationStatus) -> some View {
        Circle()
            .fill(statusColor(for: status))
            .frame(width: 8, height: 8)
    }

    private func statusColor(for status: FileValidationStatus) -> Color {
        switch status {
        case .included:
            return .green
        case .warning:
            return .orange
        case .blocked:
            return .red
        }
    }

    private var readyWorkspace: some View {
        VStack(spacing: 0) {
            if let report = viewModel.validationReport {
                WorkbookStatusStrip(report: report)
                    .environmentObject(viewModel)
                Divider()
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
                    Text("当前没有可显示的汇总结果")
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
            Text("已应用 \(viewModel.correctionCount) 个类型修正")
                .font(.caption)
                .foregroundStyle(.secondary)

            Spacer()

            Button("撤销上一步") {
                viewModel.undoLastOverride()
            }
            .font(.caption)
            .buttonStyle(.bordered)
            .controlSize(.small)

            Button("保存为 Schema…") {
                viewModel.showSaveSchemaDialog = true
            }
            .font(.caption)
            .buttonStyle(.borderedProminent)
            .controlSize(.small)

            Button("清除全部修正") {
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
        case .included: return "参与合并"
        case .warning: return "已跳过"
        case .blocked: return "阻断"
        }
    }
}

struct WorkbookStatusStrip: View {
    @EnvironmentObject var viewModel: AppViewModel
    let report: WorkbookValidationReport

    var body: some View {
        HStack(spacing: 16) {
            statusColumn(title: "参与文件", value: "\(report.includedFiles.count)")
            statusColumn(title: "当前工作表", value: viewModel.selectedSheetName ?? "-")
            statusColumn(title: "结构状态", value: viewModel.selectedSheetStructureStatus)
            statusColumn(title: "Schema", value: viewModel.matchedSchema?.name ?? "未命中")
            statusColumn(title: "人工修正", value: "\(viewModel.correctionCount)")
            statusColumn(title: "异常队列", value: "\(viewModel.allAnomalies.count)")
            if report.skippedSheetCount > 0 {
                statusColumn(title: "跳过工作表", value: "\(report.skippedSheetCount)")
            }

            Spacer()
        }
        .padding(.horizontal)
        .padding(.vertical, 10)
        .background(Color(NSColor.windowBackgroundColor))
    }

    private func statusColumn(title: String, value: String) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(value)
                .font(.subheadline)
                .fontWeight(.medium)
            Text(title)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
    }
}

struct SheetCapsuleStrip: View {
    let items: [SheetOverviewItem]
    let report: WorkbookValidationReport
    let selection: WorkspaceSheetSelection?
    let onSelectSheet: (SheetOverviewItem) -> Void

    var body: some View {
        VStack(spacing: 0) {
            HStack(alignment: .firstTextBaseline) {
                VStack(alignment: .leading, spacing: 4) {
                    Text("工作表")
                        .font(.headline)
                    Text("滚动鼠标可横向浏览全部工作表。")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Spacer()
            }
            .padding(.horizontal)
            .padding(.top, 12)
            .padding(.bottom, 8)

            HorizontalWheelScrollView {
                HStack(spacing: 8) {
                    ForEach(items) { item in
                        capsuleButton(for: item)
                    }
                }
                .padding(.horizontal)
                .padding(.bottom, 12)
            }
            .frame(height: 54)
        }
        .background(Color(NSColor.windowBackgroundColor))
    }

    private func capsuleButton(for item: SheetOverviewItem) -> some View {
        Button {
            onSelectSheet(item)
        } label: {
            HStack(spacing: 8) {
                Image(systemName: leadingIcon(for: item))
                    .font(.caption)
                    .foregroundStyle(capsuleForeground(for: item))
                Text(item.sheetName)
                    .font(.system(size: 13, weight: capsuleWeight(for: item)))
                    .foregroundStyle(capsuleForeground(for: item))
                    .lineLimit(1)
                if item.anomalyCount > 0 {
                    Text("\(item.anomalyCount)")
                        .font(.caption2)
                        .fontWeight(.semibold)
                        .padding(.horizontal, 6)
                        .padding(.vertical, 2)
                        .background(Capsule().fill(Color.orange.opacity(isSelected(item) && item.status == .mergeable ? 0.18 : 0.12)))
                        .foregroundStyle(isSelected(item) && item.status == .mergeable ? Color.accentColor : Color.orange)
                }
                if item.status == .skipped {
                    Text("已跳过")
                        .font(.caption2)
                        .fontWeight(.medium)
                        .foregroundStyle(.orange)
                }
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 8)
            .background(
                Capsule()
                    .fill(capsuleBackground(for: item))
            )
            .overlay(
                Capsule()
                    .stroke(capsuleBorder(for: item), lineWidth: 1)
            )
        }
        .buttonStyle(.plain)
        .help(tooltip(for: item))
    }

    private func leadingIcon(for item: SheetOverviewItem) -> String {
        switch item.status {
        case .mergeable:
            return isSelected(item) ? "eye.fill" : "tablecells.fill"
        case .skipped:
            return isSelected(item) ? "exclamationmark.circle.fill" : "exclamationmark.circle"
        }
    }

    private func capsuleWeight(for item: SheetOverviewItem) -> Font.Weight {
        if isSelected(item) {
            return .semibold
        }
        return .regular
    }

    private func capsuleForeground(for item: SheetOverviewItem) -> Color {
        switch item.status {
        case .mergeable:
            return isSelected(item) ? .accentColor : .primary
        case .skipped:
            return .orange
        }
    }

    private func capsuleBackground(for item: SheetOverviewItem) -> Color {
        switch item.status {
        case .mergeable:
            return isSelected(item) ? Color.accentColor.opacity(0.10) : Color(NSColor.controlBackgroundColor)
        case .skipped:
            return isSelected(item) ? Color.orange.opacity(0.12) : Color.orange.opacity(0.06)
        }
    }

    private func capsuleBorder(for item: SheetOverviewItem) -> Color {
        switch item.status {
        case .mergeable:
            return isSelected(item) ? Color.accentColor.opacity(0.35) : Color.secondary.opacity(0.12)
        case .skipped:
            return Color.orange.opacity(isSelected(item) ? 0.45 : 0.25)
        }
    }

    private func tooltip(for item: SheetOverviewItem) -> String {
        switch item.status {
        case .mergeable:
            return "\(item.sheetName)\n参与文件: \(item.participatingFileCount)/\(item.totalFileCount)\n有效尺寸: \(item.effectiveRowCount) x \(item.effectiveColumnCount)\n异常数: \(item.anomalyCount)"
        case .skipped:
            if let consensus = WorkspaceDiagnostics.buildSkippedSheetConsensus(report: report, sheetName: item.sheetName) {
                return [
                    item.sheetName,
                    consensus.summary,
                    "识别到 \(consensus.groupCount) 个结构分组"
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
                Image(systemName: "exclamationmark.triangle.fill")
                    .foregroundStyle(.orange)
                Text("\(consensus.sheetName) 未参与合并")
                    .font(.subheadline)
                    .fontWeight(.semibold)
                Text("总体比较结果")
                    .font(.caption)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 3)
                    .background(Color.orange.opacity(0.12))
                    .foregroundStyle(.orange)
                    .clipShape(Capsule())
                Spacer()
            }

            Text(consensus.summary)
                .font(.caption)
                .foregroundStyle(.secondary)

            HStack(spacing: 12) {
                consensusMetric(title: "参与比较", value: "\(consensus.comparedFileCount) 个文件")
                consensusMetric(title: "结构分组", value: "\(consensus.groupCount) 组")
                if let dominant = consensus.dominantGroupDescription {
                    consensusMetric(title: "最多文件结构", value: dominant)
                }
            }

            VStack(alignment: .leading, spacing: 8) {
                ForEach(consensus.groups) { group in
                    VStack(alignment: .leading, spacing: 6) {
                        HStack(spacing: 8) {
                            Text(group.title)
                                .font(.caption)
                                .fontWeight(.semibold)
                            Text(group.detail)
                                .font(.caption)
                                .foregroundStyle(group.isDominant ? .primary : .secondary)
                            Text("\(group.fileCount) 个文件")
                                .font(.caption)
                                .foregroundStyle(group.isDominant ? .orange : .secondary)
                            if group.isDominant {
                                Text("最多")
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
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                    .padding(10)
                    .background(group.isDominant ? Color.orange.opacity(0.08) : Color(NSColor.controlBackgroundColor))
                    .clipShape(RoundedRectangle(cornerRadius: 10))
                }
            }
        }
        .padding(12)
        .background(Color.orange.opacity(0.05))
        .clipShape(RoundedRectangle(cornerRadius: 12))
    }

    private func consensusMetric(title: String, value: String) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(value)
                .font(.caption)
                .fontWeight(.medium)
            Text(title)
                .font(.caption2)
                .foregroundStyle(.secondary)
        }
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

                    Text("\(consensus.sheetName) 已跳过")
                        .font(.title3)
                        .fontWeight(.semibold)

                    Text("这个工作表没有在当前文件集合中形成统一的有效尺寸，因此本次不进入汇总表格，也不提供检查台编辑。")
                        .font(.callout)
                        .multilineTextAlignment(.center)
                        .foregroundStyle(.secondary)
                        .frame(maxWidth: 560)
                }
                .padding(.top, 28)

                SkippedSheetConsensusCard(consensus: consensus)
                    .frame(maxWidth: 860)

                Text("如果需要合并这个工作表，请先让参与文件在该 sheet 的有效行列范围保持一致，再重新校验。")
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

private struct ScrollOffsetPreferenceKey: PreferenceKey {
    static var defaultValue: CGPoint = .zero
    static func reduce(value: inout CGPoint, nextValue: () -> CGPoint) {
        value = nextValue()
    }
}

struct ExcelGridView: View {
    let rows: [[MergedCell]]
    @Binding var selectedCell: CellPosition?
    let anomalyPositions: Set<CellPosition>
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
                    ScrollView([.horizontal, .vertical]) {
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
                        .background(
                            GeometryReader { proxy in
                                Color.clear.preference(
                                    key: ScrollOffsetPreferenceKey.self,
                                    value: CGPoint(
                                        x: max(0, -proxy.frame(in: .named(GridMetrics.coordinateSpaceName)).minX),
                                        y: max(0, -proxy.frame(in: .named(GridMetrics.coordinateSpaceName)).minY)
                                    )
                                )
                            }
                        )
                    }

                    topLeftCorner
                        .frame(width: rowNumberColumnWidth, height: GridMetrics.headerHeight)
                        .background(Color(NSColor.controlBackgroundColor))

                    columnHeaders
                        .offset(x: -scrollOffset.x)
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
                        .offset(y: -scrollOffset.y)
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
                }
                .coordinateSpace(name: GridMetrics.coordinateSpaceName)
                .onPreferenceChange(ScrollOffsetPreferenceKey.self) { offset in
                    scrollOffset = offset
                }
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
        case "3":
            applyQuickOverride(.mixed)
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
            width: contentWidth(for: colIdx),
            probe: layoutObserver.map { _ in .body(position) }
        )
        .contentShape(Rectangle())
        .onTapGesture {
            handleCellTap(position: position)
        }
        .contextMenu {
            Menu("修正为") {
                Button("标签") {
                    onApplyOverride?(position.row, position.col, .label)
                }
                Button("求和") {
                    onApplyOverride?(position.row, position.col, .sum)
                }
                Button("混合") {
                    onApplyOverride?(position.row, position.col, .mixed)
                }
            }
        }
    }

    private func initializeColumnWidths() {
        rowNumberColumnWidth = ColumnWidthCalculator.rowNumberWidth(totalRows: rows.count)
        let calculatedWidths = ColumnWidthCalculator.defaultWidths(for: rows)
        columnWidths = calculatedWidths.merging(initialColumnWidths) { _, override in override }
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
            Text("已选 \(selectedCount) 个")
                .font(.caption)
                .foregroundStyle(.secondary)

            Divider()
                .frame(height: 16)

            Button("标签") { onApply(.label) }
                .buttonStyle(.borderedProminent)
                .controlSize(.small)

            Button("求和") { onApply(.sum) }
                .buttonStyle(.borderedProminent)
                .controlSize(.small)

            Button("混合") { onApply(.mixed) }
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
            if showsAnomalyMarker {
                Circle()
                    .fill(Color.orange)
                    .frame(width: 6, height: 6)
                    .padding(4)
            }
        }
        .overlay(alignment: .bottomTrailing) {
            if cell.isOverridden {
                Image(systemName: "pencil.circle.fill")
                    .font(.caption2)
                    .foregroundStyle(.orange)
                    .padding(3)
            }
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
        case .mixed:
            return Color.orange.opacity(0.08)
        default:
            return .white
        }
    }

    private var foregroundColor: Color {
        switch cell.type {
        case .mixed:
            return .orange
        case .sum:
            return .blue
        default:
            return .primary
        }
    }
}

struct InspectionSidebar: View {
    @EnvironmentObject var viewModel: AppViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    Text("检查台")
                        .font(.headline)
                    Text("快捷键: `1/2/3` 修正类型, `J/K` 跳转异常")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Spacer()
            }
            .padding()

            Divider()

            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    cellSummarySection
                    sourceSection
                    anomalySection
                }
                .padding()
            }
            .background(Color(NSColor.controlBackgroundColor))
        }
    }

    private var cellSummarySection: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("当前单元格")
                .font(.subheadline)
                .fontWeight(.semibold)

            if let cell = viewModel.selectedMergedCell,
               let reference = viewModel.selectedCellReference {
                VStack(alignment: .leading, spacing: 12) {
                    HStack(alignment: .top) {
                        VStack(alignment: .leading, spacing: 4) {
                            Text(reference)
                                .font(.title3)
                                .fontWeight(.semibold)
                            Text(cell.displayValue.isEmpty ? "空值" : cell.displayValue)
                                .font(.body.monospaced())
                        }
                        Spacer()
                        VStack(alignment: .trailing, spacing: 6) {
                            CellTypeBadge(type: cell.type)
                            if cell.isOverridden {
                                Text("已修正")
                                    .font(.caption2)
                                    .foregroundStyle(.orange)
                            }
                        }
                    }

                    HStack(spacing: 8) {
                        confidenceBadge(value: cell.decision.confidence)
                        if cell.decision.isSuspicious {
                            labelChip("需复核", tint: .orange)
                        }
                        labelChip("自动判定 \(cell.decision.autoDetectedType.displayName)", tint: .blue)
                    }

                    Text("判定依据")
                        .font(.caption)
                        .foregroundStyle(.secondary)

                    VStack(alignment: .leading, spacing: 6) {
                        ForEach(cell.decision.decisionReasons, id: \.self) { reason in
                            Text(reason)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }

                    VStack(alignment: .leading, spacing: 8) {
                        Text("快速修正")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                        HStack(spacing: 8) {
                            Button("标签") {
                                if let selectedCell = viewModel.selectedCell {
                                    viewModel.applyCellOverride(row: selectedCell.row, col: selectedCell.col, type: .label)
                                }
                            }
                            .buttonStyle(.borderedProminent)
                            .controlSize(.small)

                            Button("求和") {
                                if let selectedCell = viewModel.selectedCell {
                                    viewModel.applyCellOverride(row: selectedCell.row, col: selectedCell.col, type: .sum)
                                }
                            }
                            .buttonStyle(.borderedProminent)
                            .controlSize(.small)

                            Button("混合") {
                                if let selectedCell = viewModel.selectedCell {
                                    viewModel.applyCellOverride(row: selectedCell.row, col: selectedCell.col, type: .mixed)
                                }
                            }
                            .buttonStyle(.borderedProminent)
                            .controlSize(.small)
                        }
                    }
                }
                .padding()
                .background(Color.white)
                .clipShape(RoundedRectangle(cornerRadius: 12))
            } else {
                placeholderCard("选择一个单元格后，可在这里查看来源、判定依据和快速修正入口。")
            }
        }
    }

    private func confidenceBadge(value: Double) -> some View {
        labelChip("置信度 \(Int((value * 100).rounded()))%", tint: value >= 0.72 ? .green : .orange)
    }

    private func labelChip(_ text: String, tint: Color) -> some View {
        Text(text)
            .font(.caption)
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(tint.opacity(0.14))
            .foregroundStyle(tint)
            .clipShape(Capsule())
    }

    private var sourceSection: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("来源明细")
                .font(.subheadline)
                .fontWeight(.semibold)

            if let cell = viewModel.selectedMergedCell {
                VStack(alignment: .leading, spacing: 8) {
                    ForEach(Array(cell.sources.enumerated()), id: \.offset) { _, source in
                        HStack(alignment: .top, spacing: 10) {
                            SourceStateBadge(state: source.state)
                            VStack(alignment: .leading, spacing: 4) {
                                Text(source.filename)
                                    .font(.caption)
                                    .fontWeight(.medium)
                                Text(sourceDisplayValue(source))
                                    .font(.system(.caption, design: .monospaced))
                                    .foregroundStyle(source.state == .value ? .primary : .secondary)
                            }
                            Spacer()
                        }
                        .padding()
                        .background(Color.white)
                        .clipShape(RoundedRectangle(cornerRadius: 10))
                    }
                }
            } else {
                placeholderCard("来源明细会按导入顺序展示，并区分真实值、空值和缺失单元格。")
            }
        }
    }

    private func sourceDisplayValue(_ source: CellSourceEntry) -> String {
        switch source.state {
        case .value:
            return source.value
        case .empty:
            return "空值"
        case .missing:
            return "缺失单元格"
        }
    }

    private var anomalySection: some View {
        let anomalies = viewModel.allAnomalies
        return VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text("异常队列")
                    .font(.subheadline)
                    .fontWeight(.semibold)
                Spacer()
                Text("\(anomalies.count) 条")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            if anomalies.isEmpty {
                placeholderCard("当前没有需要人工关注的异常单元格。")
            } else {
                AnomalyQueueList(
                    items: anomalies,
                    currentSheet: viewModel.currentSheet,
                    selectedCell: viewModel.selectedCell
                ) { item in
                    viewModel.switchToSheet(item.sheetName)
                    viewModel.selectCell(item.position)
                }
            }
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

struct SourceStateBadge: View {
    let state: CellSourceState

    var body: some View {
        Text(title)
            .font(.caption2)
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(color.opacity(0.14))
            .foregroundStyle(color)
            .clipShape(Capsule())
    }

    private var title: String {
        switch state {
        case .value:
            return "值"
        case .empty:
            return "空"
        case .missing:
            return "缺"
        }
    }

    private var color: Color {
        switch state {
        case .value:
            return .green
        case .empty:
            return .orange
        case .missing:
            return .secondary
        }
    }
}

struct AnomalyQueueList: View {
    let items: [CellAnomalyItem]
    let currentSheet: String?
    let selectedCell: CellPosition?
    let onSelect: (CellAnomalyItem) -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            SwiftUI.ForEach(Array(items.enumerated()), id: \.element.id) { _, item in
                Button {
                    onSelect(item)
                } label: {
                    HStack(alignment: .top) {
                        VStack(alignment: .leading, spacing: 4) {
                            Text("\(item.sheetName) · \(item.cellReference)")
                                .font(.caption)
                                .fontWeight(.medium)
                                .foregroundStyle(.primary)
                            Text(item.summary)
                                .font(.caption2)
                                .foregroundStyle(.secondary)
                            if !item.displayValue.isEmpty {
                                Text(item.displayValue)
                                    .font(.system(.caption2, design: .monospaced))
                                    .foregroundStyle(.secondary)
                                    .lineLimit(1)
                            }
                        }
                        Spacer()
                        if currentSheet == item.sheetName && selectedCell == item.position {
                            Image(systemName: "arrow.right.circle.fill")
                                .foregroundStyle(Color.accentColor)
                        }
                    }
                    .padding()
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(Color.white)
                    .clipShape(RoundedRectangle(cornerRadius: 10))
                }
                .buttonStyle(.plain)
            }
        }
    }
}

struct CellTypeBadge: View {
    let type: MergedCell.CellType

    var body: some View {
        Text(type.displayName)
            .font(.caption)
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(badgeColor.opacity(0.14))
            .foregroundStyle(badgeColor)
            .clipShape(Capsule())
    }

    private var badgeColor: Color {
        switch type {
        case .label:
            return .green
        case .sum:
            return .blue
        case .mixed:
            return .orange
        case .single:
            return .gray
        }
    }
}

private extension MergedCell.CellType {
    var displayName: String {
        switch self {
        case .label:
            return "标签"
        case .sum:
            return "求和"
        case .mixed:
            return "混合"
        case .single:
            return "单值"
        }
    }
}

struct DropDelegateView: DropDelegate {
    @ObservedObject var viewModel: AppViewModel

    func performDrop(info: DropInfo) -> Bool {
        let items = info.itemProviders(for: [.fileURL])

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

    func validateDrop(info: DropInfo) -> Bool {
        info.hasItemsConforming(to: [.fileURL])
    }
}
