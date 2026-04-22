import SwiftUI
import UniformTypeIdentifiers
import xlsOneCore

@main
struct XlsOneApp: App {
    @StateObject private var viewModel = AppViewModel()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(viewModel)
                .frame(minWidth: 800, minHeight: 600)
        }
        .commands {
            CommandMenu("文件") {
                Button("\(viewModel.toolbarPresentation.importTitle)...") {
                    viewModel.showOpenFileDialog()
                }
                .keyboardShortcut("o", modifiers: .command)

                Button("追加文件...") {
                    viewModel.showAddFileDialog()
                }
                .keyboardShortcut("o", modifiers: [.command, .shift])
                .disabled(!viewModel.toolbarPresentation.appendEnabled)

                Divider()

                Button("新建批次") {
                    viewModel.closeAllFiles()
                }
                .keyboardShortcut("w", modifiers: [.command, .shift])
            }

            CommandMenu("编辑") {
                Button("重新校验") {
                    viewModel.reloadFiles()
                }
                .keyboardShortcut("r", modifiers: .command)
                .disabled(viewModel.selectedFilePaths.isEmpty)

                Divider()

                Button("清除所有修正") {
                    viewModel.clearOverrides()
                }
                .disabled(viewModel.userOverrides.isEmpty)
            }
        }
    }
}

/// 应用状态管理
@MainActor
class AppViewModel: ObservableObject {
    @Published var files: [ExcelFile] = []
    @Published var selectedFilePaths: [String] = []
    @Published var availableSheets: [String] = []
    @Published var mergedResult: MergedResult?
    @Published var validationReport: WorkbookValidationReport?
    @Published var workspacePhase: WorkspacePhase = .idle
    @Published var selectedCell: CellPosition?
    @Published var anomalyQueue: [CellAnomalyItem] = []
    @Published var allAnomalies: [CellAnomalyItem] = []
    @Published var isLoading = false
    @Published var errorMessage: String?
    @Published var showError = false
    @Published var selectedSheetSelection: WorkspaceSheetSelection?

    private let parser = ExcelParser()
    private let validator = WorkbookValidator()
    private let smartMerger = SmartMerger()
    private var mergedResultsBySheet: [String: MergedResult] = [:]

    /// 显示打开文件对话框
    func showOpenFileDialog() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = true
        panel.canChooseDirectories = false
        panel.canChooseFiles = true
        panel.allowedContentTypes = [
            UTType(filenameExtension: "xlsx")!,
            UTType(filenameExtension: "xls")!
        ]

        if panel.runModal() == .OK {
            let paths = panel.urls.map { $0.path }
            loadFiles(at: paths, append: false)
        }
    }

    /// 显示添加文件对话框
    func showAddFileDialog() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = true
        panel.canChooseDirectories = false
        panel.canChooseFiles = true
        panel.allowedContentTypes = [
            UTType(filenameExtension: "xlsx")!,
            UTType(filenameExtension: "xls")!
        ]

        if panel.runModal() == .OK {
            let paths = panel.urls.map { $0.path }
            loadFiles(at: paths, append: true)
        }
    }

    /// 加载文件
    func loadFiles(at paths: [String], append: Bool) {
        selectedFilePaths = append
            ? Self.deduplicatedPaths(selectedFilePaths + paths)
            : Self.deduplicatedPaths(paths)
        revalidateSelection()
    }

    /// 关闭所有文件
    func closeAllFiles() {
        files.removeAll()
        selectedFilePaths.removeAll()
        availableSheets.removeAll()
        mergedResult = nil
        validationReport = nil
        workspacePhase = .idle
        mergedResultsBySheet.removeAll()
        selectedCell = nil
        anomalyQueue.removeAll()
        allAnomalies.removeAll()
        matchedSchema = nil
        userOverrides.removeAll()
        overrideHistory.removeAll()
        selectedSheetSelection = nil
    }

    /// 重新加载文件
    func reloadFiles() {
        revalidateSelection()
    }

    /// 重置
    func reset() {
        closeAllFiles()
    }

    /// 切换工作表
    func switchToSheet(_ sheetName: String) {
        selectedSheetSelection = .mergeable(sheetName)
        syncSelectedSheetPresentation()
        refreshSelectionAndAnomalies()
    }

    /// 切换到跳过的工作表
    func switchToSkippedSheet(_ sheetName: String) {
        selectedSheetSelection = .skipped(sheetName)
        syncSelectedSheetPresentation()
        refreshSelectionAndAnomalies()
    }

    /// 当前用户自定义的单元格覆盖
    @Published var userOverrides: [CellTypeOverride] = []

    /// 覆盖操作历史（用于撤销）
    private var overrideHistory: [OverrideOperation] = []

    /// 是否显示 Schema 管理器
    @Published var showSchemaManager = false

    /// 是否显示保存 Schema 对话框
    @Published var showSaveSchemaDialog = false

    /// 新 Schema 名称
    @Published var newSchemaName = ""

    /// 当前匹配的 Schema
    @Published var matchedSchema: MergeSchema?

    var canExport: Bool {
        workspacePhase == .ready &&
        validationReport?.readiness == .ready &&
        !files.isEmpty &&
        !mergedResultsBySheet.isEmpty
    }

    var correctionCount: Int {
        userOverrides.count
    }

    var participatingFileCount: Int {
        validationReport?.includedFiles.count ?? 0
    }

    var blockedFileCount: Int {
        validationReport?.blockedFiles.count ?? 0
    }

    var warningFileCount: Int {
        validationReport?.warningFiles.count ?? 0
    }

    var sheetOverviewItems: [SheetOverviewItem] {
        guard let validationReport else { return [] }
        return WorkspaceDiagnostics.buildSheetOverview(
            report: validationReport,
            anomalyItems: allAnomalies
        )
    }

    var toolbarPresentation: ToolbarPresentation {
        WorkspaceToolbar.buildPresentation(
            selectedFileCount: selectedFilePaths.count,
            canExport: canExport
        )
    }

    var selectedCellReference: String? {
        guard let selectedCell else { return nil }
        return WorkspaceDiagnostics.cellReference(row: selectedCell.row, col: selectedCell.col)
    }

    var currentSheet: String? {
        guard case .mergeable(let sheetName)? = selectedSheetSelection else { return nil }
        return sheetName
    }

    var selectedSheetName: String? {
        selectedSheetSelection?.sheetName
    }

    var selectedSheetStructureStatus: String {
        switch selectedSheetSelection {
        case .mergeable:
            return "可合并"
        case .skipped:
            return "已跳过"
        case .none:
            return validationReport?.readiness == .ready ? "可合并" : "-"
        }
    }

    var selectedSkippedSheetConsensus: SkippedSheetConsensus? {
        guard case .skipped(let sheetName)? = selectedSheetSelection,
              let validationReport else {
            return nil
        }
        return WorkspaceDiagnostics.buildSkippedSheetConsensus(
            report: validationReport,
            sheetName: sheetName
        )
    }

    var selectedMergedCell: MergedCell? {
        guard let selectedCell,
              let result = mergedResult,
              selectedCell.row < result.rows.count,
              selectedCell.col < result.rows[selectedCell.row].count else {
            return nil
        }
        return result.rows[selectedCell.row][selectedCell.col]
    }

    /// 更新合并结果
    private func revalidateSelection() {
        guard !selectedFilePaths.isEmpty else {
            closeAllFiles()
            return
        }

        isLoading = true
        workspacePhase = .validating

        Task {
            let batch = await parser.parseFilesWithDiagnostics(at: selectedFilePaths)
            let outcome = validator.validate(files: batch.files, parseFailures: batch.failures)

            await MainActor.run {
                validationReport = outcome.report
                files = outcome.mergeableFiles
                availableSheets = outcome.report.commonSheetNames
                isLoading = false
            }

            guard outcome.report.readiness == .ready, !outcome.mergeableFiles.isEmpty else {
                await MainActor.run {
                    workspacePhase = .blocked
                    selectedSheetSelection = nil
                    mergedResult = nil
                    mergedResultsBySheet.removeAll()
                    selectedCell = nil
                    anomalyQueue.removeAll()
                    allAnomalies.removeAll()
                    matchedSchema = nil
                }
                return
            }

            await MainActor.run {
                workspacePhase = .ready
                selectedSheetSelection = normalizedSelectedSheetSelection(report: outcome.report)
            }
            await refreshWorkspaceResults()
        }
    }

    private func refreshWorkspaceResults() async {
        guard workspacePhase == .ready, !files.isEmpty else { return }

        var resultsBySheet: [String: MergedResult] = [:]
        for sheetName in availableSheets {
            let result = await smartMerger.merge(files: files, sheetName: sheetName)
            let finalResult = smartMerger.applyOverrides(to: result, overrides: userOverrides)
            resultsBySheet[sheetName] = finalResult
        }

        let schema = await smartMerger.appliedSchema

        await MainActor.run {
            mergedResultsBySheet = resultsBySheet
            syncSelectedSheetPresentation()
            matchedSchema = schema
            refreshSelectionAndAnomalies()
        }
    }

    /// 应用单元格类型覆盖
    func applyCellOverride(row: Int, col: Int, type: CellOverrideType) {
        guard let currentSheet else { return }
        // 记录历史（用于撤销）
        let position = CellPosition(row: row, col: col)
        overrideHistory.append(.single(sheetName: currentSheet, position: position))

        // 移除已存在的同一位置覆盖
        userOverrides.removeAll {
            $0.sheetName == currentSheet &&
            $0.rowIndex == row &&
            $0.colIndex == col
        }

        // 添加新覆盖
        let override = CellTypeOverride(
            sheetName: currentSheet,
            rowIndex: row,
            colIndex: col,
            cellType: type,
            userNote: nil
        )
        userOverrides.append(override)

        // 更新显示
        Task {
            await refreshWorkspaceResults()
        }
    }

    /// 批量应用类型覆盖
    func applyBulkOverride(positions: [CellPosition], type: CellOverrideType) {
        guard let currentSheet, !positions.isEmpty else { return }

        // 记录历史（用于撤销）
        overrideHistory.append(.batch(sheetName: currentSheet, positions: positions))

        // 移除这些位置的所有现有覆盖
        for pos in positions {
            userOverrides.removeAll {
                $0.sheetName == currentSheet &&
                $0.rowIndex == pos.row &&
                $0.colIndex == pos.col
            }
        }

        // 为每个位置添加覆盖
        for pos in positions {
            let override = CellTypeOverride(
                sheetName: currentSheet,
                rowIndex: pos.row,
                colIndex: pos.col,
                cellType: type,
                userNote: nil
            )
            userOverrides.append(override)
        }

        // 更新显示
        Task {
            await refreshWorkspaceResults()
        }
    }

    /// 保存当前覆盖为 Schema
    func saveCurrentAsSchema(name: String) async throws {
        guard workspacePhase == .ready, !files.isEmpty else { return }

        let fingerprint = FingerprintGenerator.generate(from: files[0])
        _ = try await smartMerger.createSchema(
            name: name,
            fingerprint: fingerprint,
            overrides: userOverrides
        )

        // 清空临时覆盖（已保存）
        userOverrides.removeAll()
        overrideHistory.removeAll()
        await refreshWorkspaceResults()
    }

    /// 清除所有用户覆盖
    func clearOverrides() {
        userOverrides.removeAll()
        overrideHistory.removeAll()
        Task {
            await refreshWorkspaceResults()
        }
    }

    /// 撤销上一步覆盖操作
    func undoLastOverride() {
        guard let lastOperation = overrideHistory.popLast() else { return }

        switch lastOperation {
        case .single(let sheetName, let position):
            removeOverride(for: position, sheetName: sheetName)
        case .batch(let sheetName, let positions):
            for pos in positions {
                removeOverride(for: pos, sheetName: sheetName)
            }
        }

        Task {
            await refreshWorkspaceResults()
        }
    }

    /// 移除指定位置的覆盖
    private func removeOverride(for position: CellPosition, sheetName: String) {
        userOverrides.removeAll {
            $0.sheetName == sheetName &&
            $0.rowIndex == position.row &&
            $0.colIndex == position.col
        }
    }

    /// 显示 Schema 管理器
    func showSchemaManagerWindow() {
        showSchemaManager = true
    }

    func selectCell(_ position: CellPosition?) {
        selectedCell = position
        refreshSelectionAndAnomalies()
    }

    func jumpToNextAnomaly() {
        jumpAcrossAnomalies(step: 1)
    }

    func jumpToPreviousAnomaly() {
        jumpAcrossAnomalies(step: -1)
    }

    private func jumpAcrossAnomalies(step: Int) {
        guard !allAnomalies.isEmpty else { return }

        let currentID = currentSheet.flatMap { sheetName in
            selectedCell.map { "\(sheetName)|\(WorkspaceDiagnostics.cellReference(row: $0.row, col: $0.col))" }
        }

        let currentIndex = currentID.flatMap { id in
            allAnomalies.firstIndex(where: { $0.id == id })
        } ?? (step > 0 ? -1 : 0)

        let nextIndex = (currentIndex + step + allAnomalies.count) % allAnomalies.count
        let target = allAnomalies[nextIndex]
        selectedSheetSelection = .mergeable(target.sheetName)
        syncSelectedSheetPresentation()
        selectedCell = target.position
        refreshSelectionAndAnomalies()
    }

    /// 导出当前结果
    func exportResult() {
        guard canExport,
              let templatePath = validationReport?.templateFile?.filepath else { return }

        let panel = NSSavePanel()
        let exportName = ExportNaming.suggestedWorkbookName(
            from: validationReport?.files.map(\.filename) ??
                selectedFilePaths.map { URL(fileURLWithPath: $0).lastPathComponent }
        )
        panel.nameFieldStringValue = exportName
        panel.allowedContentTypes = [UTType(filenameExtension: "xlsx")!]

        if panel.runModal() == .OK, let url = panel.url {
            let results = availableSheets.compactMap { mergedResultsBySheet[$0] }
            let exporter = TemplateWorkbookExporter()
            Task {
                do {
                    try exporter.exportWorkbook(
                        templatePath: templatePath,
                        results: results,
                        to: url.path
                    )
                } catch {
                    await MainActor.run {
                        errorMessage = "导出失败: \(error.localizedDescription)"
                        showError = true
                    }
                }
            }
        }
    }

    private func refreshSelectionAndAnomalies() {
        allAnomalies = availableSheets.flatMap { sheetName in
            mergedResultsBySheet[sheetName].map(WorkspaceDiagnostics.buildAnomalyQueue(for:)) ?? []
        }

        guard let currentSheet else {
            anomalyQueue = []
            selectedCell = nil
            return
        }

        anomalyQueue = mergedResultsBySheet[currentSheet].map(WorkspaceDiagnostics.buildAnomalyQueue(for:)) ?? []

        guard let result = mergedResult else {
            selectedCell = nil
            return
        }

        if let selectedCell,
           selectedCell.row < result.rows.count,
           selectedCell.col < result.rows[selectedCell.row].count {
            return
        }

        if let firstAnomaly = anomalyQueue.first {
            selectedCell = firstAnomaly.position
        } else if !result.rows.isEmpty, !result.rows[0].isEmpty {
            selectedCell = CellPosition(row: 0, col: 0)
        } else {
            selectedCell = nil
        }
    }

    private func normalizedSelectedSheetSelection(report: WorkbookValidationReport?) -> WorkspaceSheetSelection? {
        if let selectedSheetSelection {
            switch selectedSheetSelection {
            case .mergeable(let sheetName) where availableSheets.contains(sheetName):
                return .mergeable(sheetName)
            case .skipped(let sheetName) where report?.skippedSheetNames.contains(sheetName) == true:
                return .skipped(sheetName)
            default:
                break
            }
        }

        if let firstMergeableSheet = availableSheets.first {
            return .mergeable(firstMergeableSheet)
        }

        if let firstSkippedSheet = report?.skippedSheetNames.first {
            return .skipped(firstSkippedSheet)
        }

        return nil
    }

    private func syncSelectedSheetPresentation() {
        let normalizedSelection = normalizedSelectedSheetSelection(report: validationReport)
        if normalizedSelection != selectedSheetSelection {
            selectedSheetSelection = normalizedSelection
        }

        switch normalizedSelection {
        case .mergeable(let sheetName):
            mergedResult = mergedResultsBySheet[sheetName]
        case .skipped:
            mergedResult = nil
        case .none:
            mergedResult = nil
        }
    }

    private static func deduplicatedPaths(_ paths: [String]) -> [String] {
        var seen: Set<String> = []
        var result: [String] = []
        for path in paths where !seen.contains(path) {
            seen.insert(path)
            result.append(path)
        }
        return result
    }
}

// MARK: - 覆盖操作历史

private enum OverrideOperation {
    case single(sheetName: String, position: CellPosition)
    case batch(sheetName: String, positions: [CellPosition])
}
