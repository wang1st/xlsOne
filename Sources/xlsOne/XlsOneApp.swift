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

    /// 更新合并结果
    private func updateMergedResult() {
        guard let sheetName = currentSheet else {
            mergedResult = nil
            return
        }

        mergedResult = merger.merge(files: files, sheetName: sheetName)
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
