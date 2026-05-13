import SwiftUI
import xlsOneCore
import xlsOneLicense

struct ContentView: View {
    @EnvironmentObject var viewModel: AppViewModel
    @EnvironmentObject var licenseManager: LicenseManager
    @State private var isDropTargeted = false

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
        .onChange(of: viewModel.showError) { isPresented in
            guard isPresented else { return }
            showErrorAlert()
        }
        .background {
            CenteredDialogWindow(
                isPresented: $viewModel.showSchemaManager,
                title: "当前调整记忆",
                size: NSSize(width: 600, height: 400)
            ) {
                SchemaManagerView {
                    viewModel.showSchemaManager = false
                }
            }
            .frame(width: 0, height: 0)
        }
        .background {
            CenteredDialogWindow(
                isPresented: licenseActivationSheet,
                title: "激活 表表归一",
                size: NSSize(width: 420, height: 520)
            ) {
                LicenseActivationView()
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
            title: "错误",
            message: viewModel.errorMessage ?? "未知错误",
            style: .warning
        )
        viewModel.showError = false
    }

    private var toolbar: some View {
        let presentation = viewModel.toolbarPresentation
        let hasWorkspace = !viewModel.selectedFilePaths.isEmpty

        return HStack(spacing: 10) {
            if hasWorkspace {
                HStack(spacing: 2) {
                    toolbarUtilityButton(title: "追加", systemImage: "plus", help: "向当前批次追加文件") {
                        viewModel.showAddFileDialog()
                    }
                    .disabled(!presentation.appendEnabled)

                    toolbarStripDivider

                    toolbarUtilityButton(title: "刷新", systemImage: "arrow.clockwise", help: "重新读取当前文件并刷新汇总结果") {
                        viewModel.reloadFiles()
                    }
                    .disabled(viewModel.selectedFilePaths.isEmpty)

                    toolbarUtilityButton(title: "清空", systemImage: "xmark", help: "清空当前工作区，不影响原始 Excel 文件") {
                        viewModel.closeAllFiles()
                    }
                    .disabled(viewModel.selectedFilePaths.isEmpty)
                }
                .padding(4)
                .background(Color(NSColor.controlBackgroundColor))
                .overlay(
                    RoundedRectangle(cornerRadius: 12, style: .continuous)
                        .stroke(Color.secondary.opacity(0.12), lineWidth: 1)
                )
                .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
            }

            Spacer()

            if hasWorkspace {
                toolbarPrimaryButton(
                    title: "导出 XLSX",
                    systemImage: "square.and.arrow.up",
                    prominent: presentation.exportIsProminent,
                    help: viewModel.canExport ? "导出同构汇总 Excel" : "当前没有可导出的汇总结果"
                ) {
                    viewModel.exportResult()
                }
                .disabled(!viewModel.canExport)
            }
        }
        .padding(.horizontal)
        .padding(.vertical, 10)
        .background(Color(NSColor.windowBackgroundColor))
    }

    private var toolbarStripDivider: some View {
        Rectangle()
            .fill(Color.secondary.opacity(0.10))
            .frame(width: 1, height: 18)
            .padding(.horizontal, 2)
    }

    private func toolbarPrimaryButton(
        title: String,
        systemImage: String,
        prominent: Bool,
        help: String,
        action: @escaping () -> Void
    ) -> some View {
        Button(action: action) {
            Label(title, systemImage: systemImage)
        }
        .buttonStyle(WorkspaceChromePrimaryButtonStyle(prominent: prominent))
        .help(help)
    }

    private func toolbarUtilityButton(
        title: String,
        systemImage: String,
        help: String,
        action: @escaping () -> Void
    ) -> some View {
        Button(action: action) {
            Label(title, systemImage: systemImage)
        }
        .buttonStyle(WorkspaceChromeUtilityButtonStyle())
        .help(help)
    }

    private var emptyView: some View {
        ZStack {
            LinearGradient(
                colors: [
                    Color(NSColor.controlBackgroundColor),
                    Color.white.opacity(0.92)
                ],
                startPoint: .top,
                endPoint: .bottom
            )

            RoundedRectangle(cornerRadius: 28, style: .continuous)
                .fill(Color.white.opacity(isDropTargeted ? 0.94 : 0.72))
                .frame(maxWidth: 560, maxHeight: 360)
                .overlay {
                    EmptyWorkspaceBackdrop()
                        .clipShape(RoundedRectangle(cornerRadius: 28, style: .continuous))
                }
                .overlay {
                    RoundedRectangle(cornerRadius: 28, style: .continuous)
                        .strokeBorder(dropZoneBorderColor, lineWidth: isDropTargeted ? 2 : 1)
                }
                .shadow(color: Color.black.opacity(isDropTargeted ? 0.08 : 0.04), radius: 24, y: 12)
                .overlay {
                    VStack(spacing: 18) {
                        EmptyWorkspaceArtwork(isHighlighted: isDropTargeted)

                        VStack(spacing: 8) {
                            Text(isDropTargeted ? "松手即可导入" : "拖入 Excel 文件")
                                .font(.system(size: 28, weight: .semibold))
                                .foregroundStyle(.primary)

                            Text(isDropTargeted ? "支持多个 .xlsx / .xls" : "支持多个 .xlsx / .xls，导入结构一致的文件后即可开始汇总")
                                .font(.subheadline)
                                .foregroundStyle(.secondary)
                        }
                        .multilineTextAlignment(.center)

                        Button {
                            viewModel.showOpenFileDialog()
                        } label: {
                            Label("选择文件", systemImage: "folder.badge.plus")
                        }
                        .buttonStyle(WorkspaceChromePrimaryButtonStyle(prominent: true))
                        .controlSize(.large)
                    }
                    .padding(.horizontal, 40)
                    .padding(.vertical, 36)
                }
                .padding(40)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .animation(.easeInOut(duration: 0.18), value: isDropTargeted)
    }

    private var dropZoneBorderColor: Color {
        isDropTargeted
            ? Color.accentColor.opacity(0.45)
            : Color.secondary.opacity(0.14)
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
            Text("已调整 \(viewModel.correctionCount) 处")
                .font(.caption)
                .foregroundStyle(.secondary)

            Spacer()

            Button("撤销上一步") {
                viewModel.undoLastOverride()
            }
            .font(.caption)
            .buttonStyle(.bordered)
            .controlSize(.small)

            Button("清除本批次调整") {
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
            return "\(item.sheetName)\n参与文件: \(item.participatingFileCount)/\(item.totalFileCount)\n有效尺寸: \(item.effectiveRowCount) x \(item.effectiveColumnCount)"
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

private struct WorkspaceChromePrimaryButtonStyle: ButtonStyle {
    let prominent: Bool

    func makeBody(configuration: Configuration) -> some View {
        WorkspaceChromePrimaryButton(configuration: configuration, prominent: prominent)
    }
}

private struct WorkspaceChromePrimaryButton: View {
    let configuration: WorkspaceChromePrimaryButtonStyle.Configuration
    let prominent: Bool

    @Environment(\.isEnabled) private var isEnabled

    var body: some View {
        configuration.label
            .font(.system(size: 13, weight: .semibold))
            .padding(.horizontal, 14)
            .padding(.vertical, 8)
            .foregroundStyle(foregroundColor)
            .background(
                RoundedRectangle(cornerRadius: 10, style: .continuous)
                    .fill(backgroundColor)
            )
            .overlay(
                RoundedRectangle(cornerRadius: 10, style: .continuous)
                    .stroke(borderColor, lineWidth: 1)
            )
            .shadow(color: shadowColor, radius: 8, y: 1)
            .scaleEffect(configuration.isPressed ? 0.985 : 1)
            .opacity(isEnabled ? 1 : 0.48)
            .animation(.easeOut(duration: 0.12), value: configuration.isPressed)
    }

    private var foregroundColor: Color {
        prominent && isEnabled ? .white : .primary
    }

    private var backgroundColor: Color {
        if prominent {
            let opacity = configuration.isPressed ? 0.82 : 0.94
            return Color.accentColor.opacity(isEnabled ? opacity : 0.18)
        }
        return Color.white.opacity(configuration.isPressed ? 0.88 : 0.96)
    }

    private var borderColor: Color {
        if prominent {
            return Color.accentColor.opacity(isEnabled ? 0.18 : 0.10)
        }
        return Color.secondary.opacity(0.14)
    }

    private var shadowColor: Color {
        guard prominent, isEnabled, !configuration.isPressed else { return .clear }
        return Color.black.opacity(0.08)
    }
}

private struct WorkspaceChromeUtilityButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        WorkspaceChromeUtilityButton(configuration: configuration)
    }
}

private struct EmptyWorkspaceBackdrop: View {
    var body: some View {
        ZStack {
            LinearGradient(
                colors: [
                    Color.white.opacity(0.72),
                    Color(red: 0.95, green: 0.96, blue: 0.98).opacity(0.88)
                ],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )

            VStack(spacing: 18) {
                ForEach(0..<5, id: \.self) { _ in
                    Rectangle()
                        .fill(Color.secondary.opacity(0.055))
                        .frame(height: 1)
                }
            }
            .padding(.horizontal, 32)

            HStack(spacing: 18) {
                ForEach(0..<6, id: \.self) { _ in
                    Rectangle()
                        .fill(Color.secondary.opacity(0.038))
                        .frame(width: 1)
                }
            }
            .padding(.vertical, 26)
        }
    }
}

private struct EmptyWorkspaceArtwork: View {
    let isHighlighted: Bool

    var body: some View {
        ZStack {
            sheet(width: 118, height: 92, x: -26, y: -10, opacity: 0.42)
            sheet(width: 126, height: 98, x: 24, y: -4, opacity: 0.5)

            RoundedRectangle(cornerRadius: 16, style: .continuous)
                .fill(Color.white.opacity(0.95))
                .frame(width: 138, height: 106)
                .overlay(
                    RoundedRectangle(cornerRadius: 16, style: .continuous)
                        .stroke(
                            isHighlighted ? Color.accentColor.opacity(0.38) : Color.primary.opacity(0.14),
                            lineWidth: 1.2
                        )
                )
                .overlay(alignment: .topLeading) {
                    VStack(spacing: 8) {
                        Rectangle()
                            .fill(isHighlighted ? Color.accentColor.opacity(0.22) : Color.primary.opacity(0.08))
                            .frame(height: 10)
                            .clipShape(RoundedRectangle(cornerRadius: 5, style: .continuous))

                        ForEach(0..<3, id: \.self) { _ in
                            HStack(spacing: 6) {
                                ForEach(0..<3, id: \.self) { _ in
                                    RoundedRectangle(cornerRadius: 4, style: .continuous)
                                        .fill(Color.primary.opacity(0.07))
                                        .frame(width: 32, height: 14)
                                }
                            }
                        }
                    }
                    .padding(14)
                }
        }
        .frame(width: 190, height: 124)
    }

    private func sheet(width: CGFloat, height: CGFloat, x: CGFloat, y: CGFloat, opacity: Double) -> some View {
        RoundedRectangle(cornerRadius: 14, style: .continuous)
            .fill(Color.white.opacity(0.7))
            .frame(width: width, height: height)
            .overlay(
                RoundedRectangle(cornerRadius: 14, style: .continuous)
                    .stroke(Color.primary.opacity(0.08), lineWidth: 1)
            )
            .overlay {
                VStack(spacing: 9) {
                    ForEach(0..<3, id: \.self) { _ in
                        Rectangle()
                            .fill(Color.primary.opacity(0.05))
                            .frame(height: 1)
                    }
                }
                .padding(.horizontal, 14)
            }
            .offset(x: x, y: y)
            .opacity(opacity)
    }
}

private struct WorkspaceChromeUtilityButton: View {
    let configuration: WorkspaceChromeUtilityButtonStyle.Configuration

    @Environment(\.isEnabled) private var isEnabled

    var body: some View {
        configuration.label
            .font(.system(size: 13, weight: .medium))
            .padding(.horizontal, 10)
            .padding(.vertical, 7)
            .foregroundStyle(isEnabled ? Color.primary : Color.secondary)
            .background(
                RoundedRectangle(cornerRadius: 9, style: .continuous)
                    .fill(backgroundColor)
            )
            .opacity(isEnabled ? 1 : 0.5)
            .animation(.easeOut(duration: 0.12), value: configuration.isPressed)
    }

    private var backgroundColor: Color {
        guard isEnabled else { return .clear }
        return configuration.isPressed ? Color.secondary.opacity(0.10) : .clear
    }
}

struct SkippedSheetConsensusCard: View {
    let consensus: SkippedSheetConsensus

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 8) {
                Text("结构分组")
                    .font(.subheadline)
                    .fontWeight(.semibold)
                Spacer()
                Text("\(consensus.comparedFileCount) 个文件")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Text("\(consensus.groupCount) 组")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            if let dominant = consensus.dominantGroupDescription {
                Text("主结构：\(dominant)")
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
                            Text("\(group.detail) · \(group.fileCount) 个文件")
                                .font(.caption)
                                .foregroundStyle(group.isDominant ? .primary : .secondary)
                            if group.isDominant {
                                Text("主")
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

                    Text("未参与本次汇总")
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

                Text("统一该 sheet 的有效行列范围后，可重新纳入合并。")
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
            Menu("修正为") {
                Button("标签") {
                    onApplyOverride?(position.row, position.col, .label)
                }
                Button("求和") {
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
                    placeholderCard("选择单元格后查看结果与来源。")
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
                    Text(cell.displayValue.isEmpty ? "空值" : cell.displayValue)
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
                    overrideButton(title: "标签", tint: .green, isActive: matches(cell.type, overrideType: .label)) {
                        applyOverride(.label)
                    }
                    overrideButton(title: "求和", tint: .blue, isActive: matches(cell.type, overrideType: .sum)) {
                        applyOverride(.sum)
                    }
                }

                if viewModel.canRestoreSelectedCellAutomatic {
                    Button("恢复自动判断") {
                        viewModel.restoreAutomaticDecisionForSelectedCell()
                    }
                    .buttonStyle(.borderless)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                }

                Text("按 1 或 2")
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
                placeholderCard("当前单元格没有可显示的来源。")
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
            return "当前按\(displayTypeName(for: cell.type))显示"
        }
        return WorkspaceDiagnostics.decisionSummary(for: cell)
    }

    private func displayTypeName(for cellType: MergedCell.CellType) -> String {
        switch cellType {
        case .sum:
            return "求和"
        default:
            return "标签"
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

    private func sourceStateText(for state: CellSourceState) -> String? {
        switch state {
        case .value:
            return nil
        case .empty:
            return "空值"
        case .missing:
            return "缺失"
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

private struct CenteredDialogWindow<DialogContent: View>: NSViewRepresentable {
    @Binding var isPresented: Bool
    let title: String
    let size: NSSize
    let content: () -> DialogContent

    init(
        isPresented: Binding<Bool>,
        title: String,
        size: NSSize,
        @ViewBuilder content: @escaping () -> DialogContent
    ) {
        _isPresented = isPresented
        self.title = title
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

        init(parent: CenteredDialogWindow) {
            self.parent = parent
        }

        func present(anchor: NSView) {
            if let panel {
                panel.title = parent.title
                panel.setContentSize(parent.size)
                center(panel, anchor: anchor)
                return
            }

            let rootView = parent.content()
            let panel = NSPanel(
                contentRect: NSRect(origin: .zero, size: parent.size),
                styleMask: [.titled, .closable],
                backing: .buffered,
                defer: false
            )
            panel.title = parent.title
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
