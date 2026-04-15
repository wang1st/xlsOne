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
                Button("打开文件...") {
                    viewModel.showOpenFileDialog()
                }
                .keyboardShortcut("o", modifiers: .command)

                Button("添加文件...") {
                    viewModel.showAddFileDialog()
                }
                .keyboardShortcut("o", modifiers: [.command, .shift])

                Divider()

                Button("关闭全部") {
                    viewModel.closeAllFiles()
                }
                .keyboardShortcut("w", modifiers: [.command, .shift])
            }

            CommandMenu("编辑") {
                Button("重载") {
                    viewModel.reloadFiles()
                }
                .keyboardShortcut("r", modifiers: .command)

                Button("重置") {
                    viewModel.reset()
                }
                .keyboardShortcut("t", modifiers: .command)

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
    @Published var currentSheet: String?
    @Published var availableSheets: [String] = []
    @Published var mergedResult: MergedResult?
    @Published var isLoading = false
    @Published var errorMessage: String?
    @Published var showError = false

    private let parser = ExcelParser()
    private let merger = SimpleMerger()

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
        isLoading = true

        Task {
            do {
                let newFiles = try await parser.parseFiles(at: paths)

                if append {
                    files.append(contentsOf: newFiles)
                } else {
                    files = newFiles
                }

                // 更新可用工作表
                availableSheets = merger.availableSheetNames(from: files)

                // 如果当前工作表不在列表中，选择第一个
                if let current = currentSheet, availableSheets.contains(current) {
                    // 保持当前选择
                } else {
                    currentSheet = availableSheets.first
                }

                // 重新合并
                updateMergedResult()

            } catch {
                errorMessage = error.localizedDescription
                showError = true
            }

            isLoading = false
        }
    }

    /// 关闭所有文件
    func closeAllFiles() {
        files.removeAll()
        availableSheets.removeAll()
        currentSheet = nil
        mergedResult = nil
    }

    /// 重新加载文件
    func reloadFiles() {
        let paths = files.map { $0.filepath }
        loadFiles(at: paths, append: false)
    }

    /// 重置
    func reset() {
        closeAllFiles()
    }

    /// 切换工作表
    func switchToSheet(_ sheetName: String) {
        currentSheet = sheetName
        updateMergedResult()
    }

    /// 当前用户自定义的单元格覆盖
    @Published var userOverrides: [CellTypeOverride] = []

    /// 是否显示 Schema 管理器
    @Published var showSchemaManager = false

    /// 是否显示保存 Schema 对话框
    @Published var showSaveSchemaDialog = false

    /// 新 Schema 名称
    @Published var newSchemaName = ""

    /// 当前匹配的 Schema
    @Published var matchedSchema: MergeSchema?

    private let smartMerger = SmartMerger()

    /// 更新合并结果
    private func updateMergedResult() {
        Task {
            guard let sheetName = currentSheet else {
                await MainActor.run {
                    mergedResult = nil
                }
                return
            }

            // 使用 SmartMerger 合并（会自动匹配 Schema）
            let result = await smartMerger.merge(files: files, sheetName: sheetName)

            // 应用用户临时覆盖（纯计算，无需 await）
            let finalResult = smartMerger.applyOverrides(to: result, overrides: userOverrides)

            // 获取当前应用的 Schema
            let schema = await smartMerger.appliedSchema

            await MainActor.run {
                mergedResult = finalResult
                matchedSchema = schema
            }
        }
    }

    /// 应用单元格类型覆盖
    func applyCellOverride(row: Int, col: Int, type: CellOverrideType) {
        // 移除已存在的同一位置覆盖
        userOverrides.removeAll { $0.rowIndex == row && $0.colIndex == col }

        // 添加新覆盖
        let override = CellTypeOverride(
            rowIndex: row,
            colIndex: col,
            cellType: type,
            userNote: nil
        )
        userOverrides.append(override)

        // 更新显示
        updateMergedResult()
    }

    /// 批量应用类型覆盖
    func applyBulkOverride(positions: [CellPosition], type: CellOverrideType) {
        // 移除这些位置的所有现有覆盖
        for pos in positions {
            userOverrides.removeAll { $0.rowIndex == pos.row && $0.colIndex == pos.col }
        }

        // 为每个位置添加覆盖
        for pos in positions {
            let override = CellTypeOverride(
                rowIndex: pos.row,
                colIndex: pos.col,
                cellType: type,
                userNote: nil
            )
            userOverrides.append(override)
        }

        // 更新显示
        updateMergedResult()
    }

    /// 保存当前覆盖为 Schema
    func saveCurrentAsSchema(name: String) async throws {
        guard !files.isEmpty else { return }

        let fingerprint = FingerprintGenerator.generate(from: files[0])
        _ = try await smartMerger.createSchema(
            name: name,
            fingerprint: fingerprint,
            overrides: userOverrides
        )

        // 清空临时覆盖（已保存）
        userOverrides.removeAll()
    }

    /// 清除所有用户覆盖
    func clearOverrides() {
        userOverrides.removeAll()
        updateMergedResult()
    }

    /// 显示 Schema 管理器
    func showSchemaManagerWindow() {
        showSchemaManager = true
    }

    /// 导出当前结果
    func exportResult() {
        guard let result = mergedResult else { return }

        let panel = NSSavePanel()
        panel.nameFieldStringValue = "\(result.sheetName)_汇总"
        panel.allowedContentTypes = [UTType.html]

        if panel.runModal() == .OK, let url = panel.url {
            let exporter = ExcelExporter()
            do {
                try exporter.saveHTML(result: result, to: url.path)
            } catch {
                errorMessage = "导出失败: \(error.localizedDescription)"
                showError = true
            }
        }
    }
}
