import SwiftUI
// xlsOneCore sources are now compiled directly into this target

@main
struct XlsOneApp {
    static func main() {
        let app = NSApplication.shared
        let delegate = AppDelegate()
        app.delegate = delegate
        app.run()
    }
}

class AppDelegate: NSObject, NSApplicationDelegate {
    var appState: AppState?
    
    func applicationDidFinishLaunching(_ notification: Notification) {
        appState = AppState()
        
        guard let appState = appState else { return }
        
        let contentView = ContentView()
            .environmentObject(appState)
            .frame(minWidth: 900, minHeight: 600)
        
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 900, height: 600),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "xlsOne"
        window.contentView = NSHostingView(rootView: contentView)
        window.center()
        window.makeKeyAndOrderFront(nil)
        
        appState.window = window
        setupMenu()
    }
    
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return true
    }
    
    func applicationSupportsSecureRestorableState(_ app: NSApplication) -> Bool {
        return true
    }
    
    private func setupMenu() {
        let mainMenu = NSMenu()
        
        // App menu
        let appMenuItem = NSMenuItem()
        mainMenu.addItem(appMenuItem)
        let appMenu = NSMenu()
        appMenuItem.submenu = appMenu
        appMenu.addItem(withTitle: "关于 xlsOne", action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)), keyEquivalent: "")
        appMenu.addItem(NSMenuItem.separator())
        appMenu.addItem(withTitle: "退出 xlsOne", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        
        // File menu
        let fileMenuItem = NSMenuItem()
        mainMenu.addItem(fileMenuItem)
        let fileMenu = NSMenu(title: "文件")
        fileMenuItem.submenu = fileMenu
        fileMenu.addItem(withTitle: "打开文件...", action: #selector(openFile), keyEquivalent: "o")
        fileMenu.addItem(NSMenuItem.separator())
        fileMenu.addItem(withTitle: "导出...", action: #selector(exportFile), keyEquivalent: "s")
        
        // Edit menu
        let editMenuItem = NSMenuItem()
        mainMenu.addItem(editMenuItem)
        let editMenu = NSMenu(title: "编辑")
        editMenuItem.submenu = editMenu
        editMenu.addItem(withTitle: "重载", action: #selector(reload), keyEquivalent: "r")
        editMenu.addItem(withTitle: "重置", action: #selector(reset), keyEquivalent: "t")
        
        NSApplication.shared.mainMenu = mainMenu
    }
    
    @MainActor
    @objc func openFile() {
        appState?.openFilePicker()
    }
    
    @MainActor
    @objc func exportFile() {
        appState?.exportFile()
    }
    
    @MainActor
    @objc func reload() {
        Task { await appState?.reload() }
    }
    
    @MainActor
    @objc func reset() {
        appState?.reset()
    }
}

@MainActor
class AppState: ObservableObject {
    @Published var loadedFiles: [LoadedFile] = []
    @Published var mergedData: [[MergedCell]]?
    @Published var statistics: MergeStatistics?
    @Published var selectedCell: CellPosition?
    @Published var selectedSheet: Int = 0
    @Published var sheetNames: [String] = []
    @Published var isProcessing: Bool = false
    @Published var errorMessage: String?
    @Published var statusMessage: String = "拖拽 Excel 文件到此处开始汇总"
    
    weak var window: NSWindow?
    
    private let parser = ExcelParser()
    private let engine = MergerEngine()
    
    struct LoadedFile: Identifiable {
        let id = UUID()
        let url: URL
        let filename: String
        var data: [[CellData]]?
        var sheets: [String: [[CellData]]]
    }
    
    func openFilePicker() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = true
        panel.canChooseDirectories = false
        panel.canChooseFiles = true
        panel.allowedContentTypes = [.init(filenameExtension: "xlsx")!, .init(filenameExtension: "xls")!]
        panel.message = "选择要合并的 Excel 文件"
        
        if panel.runModal() == .OK {
            Task { await addFiles(urls: panel.urls) }
        }
    }
    
    func addFiles(urls: [URL]) async {
        isProcessing = true
        statusMessage = "正在解析文件..."
        
        for url in urls {
            if loadedFiles.contains(where: { $0.url == url }) { continue }
            
            do {
                let sheets = try await parser.parseAllSheets(filePath: url.path)
                let firstSheet = sheets.keys.sorted().first
                let data = firstSheet.flatMap { sheets[$0] }
                let file = LoadedFile(url: url, filename: url.lastPathComponent, data: data, sheets: sheets)
                loadedFiles.append(file)
            } catch {
                errorMessage = "解析失败: \(url.lastPathComponent) - \(error.localizedDescription)"
            }
        }
        
        refreshSheetNames()
        await performMerge()
    }
    
    func removeFile(_ file: LoadedFile) {
        loadedFiles.removeAll { $0.id == file.id }
        refreshSheetNames()
        Task { await performMerge() }
    }
    
    func reload() async {
        isProcessing = true
        statusMessage = "正在重新加载..."
        
        var reloadedFiles: [LoadedFile] = []
        for var file in loadedFiles {
            do {
                let sheets = try await parser.parseAllSheets(filePath: file.url.path)
                let firstSheet = sheets.keys.sorted().first
                file.data = firstSheet.flatMap { sheets[$0] }
                file.sheets = sheets
                reloadedFiles.append(file)
            } catch {
                errorMessage = "重新加载失败: \(file.filename)"
            }
        }
        
        loadedFiles = reloadedFiles
        refreshSheetNames()
        await performMerge()
    }
    
    func reset() {
        loadedFiles.removeAll()
        mergedData = nil
        statistics = nil
        selectedCell = nil
        selectedSheet = 0
        sheetNames = []
        statusMessage = "拖拽 Excel 文件到此处开始汇总"
    }
    
    func exportFile() {
        guard mergedData != nil else { return }
        
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.init(filenameExtension: "xlsx")!]
        panel.nameFieldStringValue = "merged_result.xlsx"
        
        if panel.runModal() == .OK {
            statusMessage = "导出功能开发中..."
        }
    }
    
    private func performMerge() async {
        guard !loadedFiles.isEmpty else {
            mergedData = nil
            statistics = nil
            isProcessing = false
            statusMessage = "拖拽 Excel 文件到此处开始汇总"
            return
        }
        
        statusMessage = "正在汇总..."
        let sheetName = sheetNames.indices.contains(selectedSheet) ? sheetNames[selectedSheet] : nil
        let dataSets = loadedFiles.compactMap { file in
            if let sheetName = sheetName {
                return file.sheets[sheetName]
            }
            return file.data
        }
        guard !dataSets.isEmpty else {
            isProcessing = false
            return
        }
        
        let result = engine.merge(dataSets: dataSets)
        mergedData = result.cells
        statistics = result.statistics
        isProcessing = false
        
        let fileCount = loadedFiles.count
        let cellCount = result.statistics.totalCells
        statusMessage = "已汇总 \(fileCount) 个文件，共 \(cellCount) 个单元格"
    }
    
    func refreshMergeForSelectedSheet() async {
        isProcessing = true
        await performMerge()
    }
    
    private func refreshSheetNames() {
        let names = loadedFiles.first?.sheets.keys.sorted() ?? []
        sheetNames = names
        if selectedSheet >= sheetNames.count {
            selectedSheet = 0
        }
    }
}
