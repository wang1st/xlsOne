import AppKit
import SwiftUI
import UniformTypeIdentifiers
import xlsOneCore

public struct XlsOneWorkspaceScene: Scene {
    @StateObject private var viewModel = AppViewModel()

    public init() {}

    public var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(viewModel)
                .frame(minWidth: 800, minHeight: 600)
                .onAppear {
                    WorkspaceMenuLocalizer.scheduleMainMenuLocalizationPasses()
                }
        }
        .commands {
            CommandGroup(replacing: .appInfo) {
                Button("关于 \(WorkspaceAppPresentation.displayName)") {
                    WorkspaceAppPresentation.showAboutPanel()
                }
            }

            CommandGroup(replacing: .appSettings) {
                Button("调整记忆...") {
                    viewModel.showSchemaManagerWindow()
                }
                .keyboardShortcut(",", modifiers: .command)
            }

            CommandGroup(replacing: .newItem) {
                Button("新建批次") {
                    viewModel.closeAllFiles()
                }
                .keyboardShortcut("n", modifiers: .command)
            }

            CommandGroup(after: .newItem) {
                Divider()

                Button("导入文件...") {
                    viewModel.showOpenFileDialog()
                }
                .keyboardShortcut("o", modifiers: .command)

                Button("追加文件...") {
                    viewModel.showAddFileDialog()
                }
                .keyboardShortcut("o", modifiers: [.command, .shift])
                .disabled(!viewModel.toolbarPresentation.appendEnabled)
            }

            CommandGroup(replacing: .saveItem) {
                Button("导出 XLSX...") {
                    viewModel.exportResult()
                }
                .keyboardShortcut("s", modifiers: .command)
                .disabled(!viewModel.canExport)
            }

            CommandGroup(after: .saveItem) {
                Divider()

                Button("刷新") {
                    viewModel.reloadFiles()
                }
                .keyboardShortcut("r", modifiers: .command)
                .disabled(viewModel.selectedFilePaths.isEmpty)
            }

            CommandGroup(replacing: .undoRedo) {
                Button("撤销上一步") {
                    viewModel.undoLastOverride()
                }
                .keyboardShortcut("z", modifiers: .command)
                .disabled(!viewModel.canUndoOverride)

                Divider()

                Button("清除所有修正") {
                    viewModel.clearOverrides()
                }
                .disabled(viewModel.correctionCount == 0)
            }

            CommandGroup(after: .help) {
                Divider()

                Button("关于 \(WorkspaceAppPresentation.displayName)") {
                    WorkspaceAppPresentation.showAboutPanel()
                }
            }
        }
    }
}

public final class WorkspaceAppDelegate: NSObject, NSApplicationDelegate {
    private var menuObserver: NSObjectProtocol?

    public func applicationDidFinishLaunching(_ notification: Notification) {
        menuObserver = NotificationCenter.default.addObserver(
            forName: NSMenu.didBeginTrackingNotification,
            object: nil,
            queue: .main
        ) { notification in
            guard let menu = notification.object as? NSMenu else { return }
            Task { @MainActor in
                WorkspaceMenuLocalizer.localize(menu: menu)
            }
        }
        scheduleMenuLocalization()
    }

    public func applicationDidBecomeActive(_ notification: Notification) {
        scheduleMenuLocalization()
    }

    public func applicationWillTerminate(_ notification: Notification) {
        if let menuObserver {
            NotificationCenter.default.removeObserver(menuObserver)
        }
    }

    private func scheduleMenuLocalization() {
        Task { @MainActor in
            WorkspaceMenuLocalizer.scheduleMainMenuLocalizationPasses()
        }
    }
}

enum WorkspaceAppPresentation {
    static var displayName: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleDisplayName") as? String ??
        Bundle.main.object(forInfoDictionaryKey: "CFBundleName") as? String ??
        "xlsOne"
    }

    static var versionLabel: String {
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String
        let build = Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String

        switch (version, build) {
        case let (.some(version), .some(build)) where !version.isEmpty && !build.isEmpty && version != build:
            return "版本 \(version) (\(build))"
        case let (.some(version), _):
            return "版本 \(version)"
        case let (_, .some(build)):
            return "构建 \(build)"
        default:
            return "版本信息不可用"
        }
    }

    @MainActor
    static func showAboutPanel() {
        let options: [NSApplication.AboutPanelOptionKey: Any] = [
            .applicationName: displayName,
            .applicationVersion: versionLabel,
            .version: versionLabel
        ]
        NSApp.orderFrontStandardAboutPanel(options: options)
        NSApp.activate(ignoringOtherApps: true)
    }
}

private enum WorkspaceMenuLocalizer {
    private static let topLevelTitles: [String: String] = [
        "File": "文件",
        "Edit": "编辑",
        "View": "显示",
        "Window": "窗口",
        "Help": "帮助"
    ]

    private static let directTitleMap: [String: String] = [
        "Services": "服务",
        "Services Settings…": "服务设置…",
        "Show All": "全部显示",
        "Hide Others": "隐藏其他",
        "Close": "关闭",
        "Minimize": "最小化",
        "Zoom": "缩放",
        "Bring All to Front": "前置全部窗口",
        "Quit and Keep Windows": "退出并保留窗口",
        "Undo": "撤销",
        "Redo": "重做",
        "Cut": "剪切",
        "Copy": "复制",
        "Paste": "粘贴",
        "Delete": "删除",
        "Select All": "全选",
        "Start Dictation…": "开始听写…",
        "Emoji & Symbols": "表情与符号",
        "Enter Full Screen": "进入全屏",
        "Exit Full Screen": "退出全屏"
    ]

    @MainActor
    static func scheduleMainMenuLocalizationPasses() {
        let delays: [TimeInterval] = [0, 0.2, 0.75, 1.5]
        for delay in delays {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay) {
                localizeMainMenu()
            }
        }
    }

    @MainActor
    static func localizeMainMenu() {
        guard let mainMenu = NSApp.mainMenu else { return }

        for item in mainMenu.items {
            localizeTopLevelItem(item)
        }
        mainMenu.update()
    }

    @MainActor
    static func localize(menu: NSMenu) {
        for item in menu.items {
            localizeMenuItem(item)
        }
        menu.update()
    }

    @MainActor
    private static func localizeTopLevelItem(_ item: NSMenuItem) {
        if let localizedTitle = topLevelTitles[item.title] {
            item.title = localizedTitle
            item.submenu?.title = localizedTitle
        }

        guard let submenu = item.submenu else { return }
        for child in submenu.items {
            localizeMenuItem(child)
        }
    }

    @MainActor
    private static func localizeMenuItem(_ item: NSMenuItem) {
        let appName = WorkspaceAppPresentation.displayName
        let title = item.title

        if item.action == #selector(NSApplication.orderFrontStandardAboutPanel(_:)) ||
            title == "About \(appName)" {
            item.title = "关于 \(appName)"
        } else if item.action == #selector(NSApplication.hide(_:)) ||
            title == "Hide \(appName)" {
            item.title = "隐藏 \(appName)"
        } else if item.action == #selector(NSApplication.terminate(_:)) ||
            title == "Quit \(appName)" {
            item.title = "退出 \(appName)"
        } else if let localized = directTitleMap[title] {
            item.title = localized
        }

        if let submenu = item.submenu {
            submenu.title = item.title
            for child in submenu.items {
                localizeMenuItem(child)
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
            loadFiles(at: paths, append: !selectedFilePaths.isEmpty)
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
        workspaceRuleState = .none
        userOverrides.removeAll()
        forgottenOverrideKeys.removeAll()
        overrideHistory.removeAll()
        workspaceBaseOverrides.removeAll()
        workspaceBaseSchemaID = nil
        workspaceActiveSchemaID = nil
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

    /// 本批次明确要求移除的已记住调整
    private var forgottenOverrideKeys: Set<StoredOverrideKey> = []

    /// 当前工作区在本批次开始前的已记住调整快照
    private var workspaceBaseOverrides: [CellTypeOverride] = []
    private var workspaceBaseSchemaID: UUID?
    private var workspaceActiveSchemaID: UUID?

    /// 是否显示规则管理器
    @Published var showSchemaManager = false

    /// 当前匹配的规则
    @Published var matchedSchema: MergeSchema?

    /// 当前工作区的规则应用状态
    @Published var workspaceRuleState: WorkspaceRuleState = .none

    var canExport: Bool {
        workspacePhase == .ready &&
        validationReport?.readiness == .ready &&
        !files.isEmpty &&
        !mergedResultsBySheet.isEmpty
    }

    var correctionCount: Int {
        userOverrides.count + forgottenOverrideKeys.count
    }

    var canUndoOverride: Bool {
        !overrideHistory.isEmpty
    }

    var manualOverridePositionsForCurrentSheet: Set<CellPosition> {
        guard let currentSheet else { return [] }
        return Set(
            userOverrides
                .filter { $0.sheetName == currentSheet }
                .map { CellPosition(row: $0.rowIndex, col: $0.colIndex) }
        )
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

    func correctionState(for position: CellPosition, cell: MergedCell? = nil) -> CellCorrectionState {
        if manualOverridePositionsForCurrentSheet.contains(position) {
            return .manual
        }

        if let cell {
            return cell.isOverridden ? .rule : .none
        }

        guard let result = mergedResult,
              position.row < result.rows.count,
              position.col < result.rows[position.row].count else {
            return .none
        }

        return result.rows[position.row][position.col].isOverridden ? .rule : .none
    }

    var canRestoreSelectedCellAutomatic: Bool {
        guard let currentSheet,
              let selectedCell else {
            return false
        }

        return effectiveWorkspaceOverrides().contains {
            $0.sheetName == currentSheet &&
            $0.rowIndex == selectedCell.row &&
            $0.colIndex == selectedCell.col
        }
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
                    workspaceRuleState = .none
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

        let ruleMatchResult = await smartMerger.prepareWorkspaceSchema(
            files: files,
            sheetNames: availableSheets
        )
        let workspaceSchema: MergeSchema?
        switch ruleMatchResult {
        case .exact(let schema):
            workspaceSchema = schema
        case .ambiguous, .similar, .none:
            workspaceSchema = nil
        }

        syncWorkspaceMemoryBase(with: workspaceSchema)
        let effectiveOverrides = effectiveWorkspaceOverrides()
        var resultsBySheet: [String: MergedResult] = [:]
        for sheetName in availableSheets {
            let result = await smartMerger.merge(
                files: files,
                sheetName: sheetName,
                applying: nil
            )
            let finalResult = smartMerger.applyOverrides(to: result, overrides: effectiveOverrides)
            resultsBySheet[sheetName] = finalResult
        }

        await MainActor.run {
            mergedResultsBySheet = resultsBySheet
            syncSelectedSheetPresentation()
            matchedSchema = workspaceSchema
            workspaceRuleState = Self.ruleState(from: ruleMatchResult)
            refreshSelectionAndAnomalies()
        }
    }

    /// 应用单元格类型覆盖
    func applyCellOverride(row: Int, col: Int, type: CellOverrideType) {
        guard let currentSheet else { return }
        recordOverrideSnapshot()
        userOverrides.removeAll {
            $0.sheetName == currentSheet &&
            $0.rowIndex == row &&
            $0.colIndex == col
        }
        forgottenOverrideKeys.remove(
            StoredOverrideKey(sheetName: currentSheet, rowIndex: row, colIndex: col)
        )

        let override = CellTypeOverride(
            sheetName: currentSheet,
            rowIndex: row,
            colIndex: col,
            cellType: type,
            userNote: nil
        )
        userOverrides.append(override)

        Task {
            await persistAdjustmentMemoryAndRefresh()
        }
    }

    /// 批量应用类型覆盖
    func applyBulkOverride(positions: [CellPosition], type: CellOverrideType) {
        guard let currentSheet, !positions.isEmpty else { return }

        recordOverrideSnapshot()
        for pos in positions {
            userOverrides.removeAll {
                $0.sheetName == currentSheet &&
                $0.rowIndex == pos.row &&
                $0.colIndex == pos.col
            }
            forgottenOverrideKeys.remove(
                StoredOverrideKey(sheetName: currentSheet, rowIndex: pos.row, colIndex: pos.col)
            )
        }

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

        Task {
            await persistAdjustmentMemoryAndRefresh()
        }
    }

    /// 清除所有用户覆盖
    func clearOverrides() {
        guard correctionCount > 0 else { return }
        recordOverrideSnapshot()
        userOverrides.removeAll()
        forgottenOverrideKeys.removeAll()
        Task {
            await persistAdjustmentMemoryAndRefresh()
        }
    }

    /// 撤销上一步覆盖操作
    func undoLastOverride() {
        guard let lastOperation = overrideHistory.popLast() else { return }

        switch lastOperation {
        case .snapshot(let previousOverrides, let previousForgottenKeys):
            userOverrides = previousOverrides
            forgottenOverrideKeys = previousForgottenKeys
        }

        Task {
            await persistAdjustmentMemoryAndRefresh()
        }
    }

    /// 显示规则管理器
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

    func restoreAutomaticDecisionForSelectedCell() {
        guard let currentSheet,
              let selectedCell,
              canRestoreSelectedCellAutomatic else {
            return
        }

        recordOverrideSnapshot()
        userOverrides.removeAll {
            $0.sheetName == currentSheet &&
            $0.rowIndex == selectedCell.row &&
            $0.colIndex == selectedCell.col
        }

        let key = StoredOverrideKey(sheetName: currentSheet, rowIndex: selectedCell.row, colIndex: selectedCell.col)
        if workspaceBaseOverrides.contains(where: {
            StoredOverrideKey(override: $0) == key
        }) {
            forgottenOverrideKeys.insert(key)
        } else {
            forgottenOverrideKeys.remove(key)
        }

        Task {
            await persistAdjustmentMemoryAndRefresh()
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

    private static func ruleState(from matchResult: SchemaMatchResult) -> WorkspaceRuleState {
        switch matchResult {
        case .exact(let schema):
            return .applied(name: schema.name, correctionCount: schema.cellOverrides.count)
        case .ambiguous(let candidates):
            return .ambiguous(count: candidates.count)
        case .similar(let candidates):
            return .similar(count: candidates.count)
        case .none:
            return .none
        }
    }

    private static func mergedRuleOverrides(
        existing: [CellTypeOverride],
        updates: [CellTypeOverride]
    ) -> [CellTypeOverride] {
        var merged = existing

        for update in updates {
            merged.removeAll {
                $0.sheetName == update.sheetName &&
                $0.rowIndex == update.rowIndex &&
                $0.colIndex == update.colIndex
            }
            merged.append(update)
        }

        return merged
    }

    private func recordOverrideSnapshot() {
        overrideHistory.append(
            .snapshot(
                previousOverrides: userOverrides,
                previousForgottenKeys: forgottenOverrideKeys
            )
        )
    }

    private func syncWorkspaceMemoryBase(with workspaceSchema: MergeSchema?) {
        guard userOverrides.isEmpty, forgottenOverrideKeys.isEmpty else { return }
        workspaceBaseOverrides = workspaceSchema?.cellOverrides ?? []
        workspaceBaseSchemaID = workspaceSchema?.id
        workspaceActiveSchemaID = workspaceSchema?.id
    }

    private func effectiveWorkspaceOverrides() -> [CellTypeOverride] {
        let rememberedOverrides = workspaceBaseOverrides.filter {
            !forgottenOverrideKeys.contains(StoredOverrideKey(override: $0))
        }
        return Self.mergedRuleOverrides(existing: rememberedOverrides, updates: userOverrides)
    }

    private func persistAdjustmentMemoryAndRefresh() async {
        do {
            try await persistAdjustmentMemory()
        } catch {
            errorMessage = "记住调整失败: \(error.localizedDescription)"
            showError = true
        }
        await refreshWorkspaceResults()
    }

    private func persistAdjustmentMemory() async throws {
        guard workspacePhase == .ready, !files.isEmpty, !availableSheets.isEmpty else { return }

        let fingerprint = FingerprintGenerator.generateWorkspaceLegacyFingerprint(
            from: files,
            sheetNames: availableSheets
        )
        let workbookFingerprint = FingerprintGenerator.generateWorkbook(
            from: files,
            sheetNames: availableSheets
        )
        let effectiveOverrides = effectiveWorkspaceOverrides()
        let schemaID = workspaceActiveSchemaID ?? workspaceBaseSchemaID

        if effectiveOverrides.isEmpty {
            if let schemaID {
                if workspaceBaseSchemaID == nil {
                    try await smartMerger.deleteSchema(id: schemaID)
                    workspaceActiveSchemaID = nil
                } else {
                    _ = try await smartMerger.updateSchema(
                        id: schemaID,
                        overrides: [],
                        fingerprint: fingerprint,
                        workbookFingerprint: workbookFingerprint
                    )
                    workspaceActiveSchemaID = schemaID
                }
            }
            return
        }

        if let schemaID {
            _ = try await smartMerger.updateSchema(
                id: schemaID,
                overrides: effectiveOverrides,
                fingerprint: fingerprint,
                workbookFingerprint: workbookFingerprint
            )
            workspaceActiveSchemaID = schemaID
        } else {
            let schema = try await smartMerger.createSchema(
                name: suggestedAdjustmentMemoryName(),
                fingerprint: fingerprint,
                workbookFingerprint: workbookFingerprint,
                overrides: effectiveOverrides
            )
            workspaceActiveSchemaID = schema.id
        }
    }

    private func suggestedAdjustmentMemoryName() -> String {
        let exportName = ExportNaming.suggestedWorkbookName(
            from: validationReport?.files.map(\.filename) ??
                selectedFilePaths.map { URL(fileURLWithPath: $0).lastPathComponent }
        )

        if exportName.hasSuffix("_汇总") {
            return String(exportName.dropLast("_汇总".count)) + "_调整记忆"
        }
        if exportName.hasSuffix("汇总") {
            return String(exportName.dropLast("汇总".count)) + "调整记忆"
        }
        return "\(exportName)_调整记忆"
    }
}

// MARK: - 覆盖操作历史

private enum OverrideOperation {
    case snapshot(previousOverrides: [CellTypeOverride], previousForgottenKeys: Set<StoredOverrideKey>)
}

private struct StoredOverrideKey: Hashable {
    let sheetName: String
    let rowIndex: Int
    let colIndex: Int

    init(sheetName: String, rowIndex: Int, colIndex: Int) {
        self.sheetName = sheetName
        self.rowIndex = rowIndex
        self.colIndex = colIndex
    }

    init(override: CellTypeOverride) {
        self.sheetName = override.sheetName ?? ""
        self.rowIndex = override.rowIndex
        self.colIndex = override.colIndex
    }
}
