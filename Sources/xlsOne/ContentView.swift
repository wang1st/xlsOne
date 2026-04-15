import SwiftUI
import xlsOneCore

struct ContentView: View {
    @EnvironmentObject var viewModel: AppViewModel

    var body: some View {
        VStack(spacing: 0) {
            // 工具栏
            toolbar

            Divider()

            // 工作表选择器
            if !viewModel.availableSheets.isEmpty {
                sheetSelector
                Divider()
            }

            // 主内容区
            if viewModel.isLoading {
                loadingView
            } else if let result = viewModel.mergedResult {
                spreadsheetView(result: result)
            } else if viewModel.files.isEmpty {
                emptyView
            } else {
                noDataView
            }
        }
        .alert("错误", isPresented: $viewModel.showError) {
            Button("确定", role: .cancel) {}
        } message: {
            Text(viewModel.errorMessage ?? "未知错误")
        }
        .onDrop(of: [.fileURL], delegate: DropDelegateView(viewModel: viewModel))
    }

    // MARK: - 子视图

    private var toolbar: some View {
        HStack(spacing: 12) {
            // 左侧按钮组
            HStack(spacing: 8) {
                Button {
                    viewModel.showOpenFileDialog()
                } label: {
                    Label("打开", systemImage: "folder")
                }
                .buttonStyle(.borderedProminent)

                Button {
                    viewModel.exportResult()
                } label: {
                    Label("导出", systemImage: "square.and.arrow.up")
                }
                .buttonStyle(.bordered)
                .disabled(viewModel.mergedResult == nil)

                Button {
                    viewModel.showSchemaManagerWindow()
                } label: {
                    Label("Schema", systemImage: "doc.text.magnifyingglass")
                }
                .buttonStyle(.bordered)
            }

            Divider()
                .frame(height: 20)

            // 中间按钮组（文件操作）
            HStack(spacing: 8) {
                Button {
                    viewModel.reloadFiles()
                } label: {
                    Label("重载", systemImage: "arrow.clockwise")
                }
                .buttonStyle(.bordered)
                .disabled(viewModel.files.isEmpty)
                .help("重新加载所有文件 (⌘R)")

                Button {
                    viewModel.closeAllFiles()
                } label: {
                    Label("清空", systemImage: "trash")
                }
                .buttonStyle(.bordered)
                .disabled(viewModel.files.isEmpty)
                .help("关闭所有文件 (⌘⇧W)")
            }

            Spacer()

            // 右侧文件计数
            if !viewModel.files.isEmpty {
                HStack(spacing: 4) {
                    Image(systemName: "doc.text")
                        .font(.caption)
                    Text("\(viewModel.files.count) 个文件")
                        .font(.caption)
                }
                .foregroundStyle(.secondary)
                .padding(.horizontal, 8)
                .padding(.vertical, 4)
                .background(Color(NSColor.controlBackgroundColor))
                .cornerRadius(4)
            }
        }
        .padding()
        .background(Color(NSColor.windowBackgroundColor))
    }

    private var sheetSelector: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 4) {
                ForEach(viewModel.availableSheets, id: \.self) { sheetName in
                    SheetTabButton(
                        title: sheetName,
                        isSelected: viewModel.currentSheet == sheetName
                    ) {
                        viewModel.switchToSheet(sheetName)
                    }
                }
            }
            .padding(.horizontal)
            .padding(.vertical, 6)
        }
        .background(Color(NSColor.windowBackgroundColor))
    }

    private var loadingView: some View {
        VStack {
            ProgressView()
                .scaleEffect(1.5)
            Text("正在加载...")
                .foregroundStyle(.secondary)
                .padding(.top)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(NSColor.controlBackgroundColor))
    }

    private var emptyView: some View {
        VStack(spacing: 20) {
            Image(systemName: "doc.text.magnifyingglass")
                .font(.system(size: 64))
                .foregroundStyle(.secondary)

            Text("拖拽 Excel 文件到此处")
                .font(.title2)
                .fontWeight(.medium)

            Text("或使用 ⌘O 打开文件")
                .font(.subheadline)
                .foregroundStyle(.secondary)

            Button("打开文件") {
                viewModel.showOpenFileDialog()
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.large)
            .padding(.top)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(NSColor.controlBackgroundColor))
    }

    private var noDataView: some View {
        VStack {
            Image(systemName: "exclamationmark.triangle")
                .font(.system(size: 48))
                .foregroundStyle(.secondary)
            Text("无法读取数据")
                .foregroundStyle(.secondary)
                .padding(.top)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(NSColor.controlBackgroundColor))
    }

    private func spreadsheetView(result: MergedResult) -> some View {
        VStack(spacing: 0) {
            // 表头信息
            HStack {
                Text("工作表: \(result.sheetName)")
                    .font(.subheadline)
                    .fontWeight(.medium)
                    .foregroundStyle(.primary)

                Spacer()

                Text("来源: \(result.sourceFiles.count) 个文件")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .padding(.horizontal)
            .padding(.vertical, 6)
            .background(Color(NSColor.windowBackgroundColor))

            Divider()

            // 表格
            ExcelGridView(
                rows: result.rows,
                onApplyOverride: { row, col, type in
                    viewModel.applyCellOverride(row: row, col: col, type: type)
                },
                onApplyBulkOverride: { positions, type in
                    viewModel.applyBulkOverride(positions: positions, type: type)
                }
            )

            // 批量操作提示
            if !viewModel.userOverrides.isEmpty {
                HStack {
                    Text("已应用 \(viewModel.userOverrides.count) 个修正")
                        .font(.caption)
                        .foregroundStyle(.secondary)

                    Spacer()

                    Button("保存为 Schema...") {
                        viewModel.showSaveSchemaDialog = true
                    }
                    .font(.caption)
                    .buttonStyle(.borderedProminent)
                    .controlSize(.small)

                    Button("清除") {
                        viewModel.clearOverrides()
                    }
                    .font(.caption)
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                }
                .padding(.horizontal)
                .padding(.vertical, 6)
                .background(Color(NSColor.controlBackgroundColor))
            }
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
            Text("为此文件类型保存当前的修正配置")
        }
    }
}

// MARK: - 工作表标签按钮

struct SheetTabButton: View {
    let title: String
    let isSelected: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 12))
                .fontWeight(isSelected ? .semibold : .regular)
                .padding(.horizontal, 12)
                .padding(.vertical, 6)
                .background(isSelected ? Color.white : Color.clear)
                .foregroundStyle(isSelected ? .primary : .secondary)
                .cornerRadius(4)
                .overlay(
                    Group {
                        if isSelected {
                            RoundedRectangle(cornerRadius: 4)
                                .stroke(Color.gray.opacity(0.3), lineWidth: 0.5)
                                .shadow(color: .black.opacity(0.05), radius: 1, x: 0, y: 1)
                        }
                    }
                )
        }
        .buttonStyle(.plain)
    }
}

// MARK: - Excel 风格网格视图

struct ExcelGridView: View {
    let rows: [[MergedCell]]
    @State private var selectedCells: Set<CellPosition> = []
    @State private var lastSelectedCell: CellPosition?
    @State private var showDetailSheet = false
    @State private var showBulkEditSheet = false

    private var maxCols: Int {
        rows.map { $0.count }.max() ?? 0
    }

    var onApplyOverride: ((Int, Int, CellOverrideType) -> Void)?
    var onApplyBulkOverride: (([CellPosition], CellOverrideType) -> Void)?

    var body: some View {
        GeometryReader { geometry in
            ScrollView([.horizontal, .vertical]) {
                VStack(alignment: .leading, spacing: 0) {
                    // 列头
                    columnHeaderRow

                    // 数据行
                    ForEach(Array(rows.enumerated()), id: \.offset) { rowIdx, row in
                        HStack(spacing: 0) {
                            // 行号
                            rowNumberView(rowIdx: rowIdx)

                            // 单元格
                            ForEach(Array(row.enumerated()), id: \.offset) { colIdx, cell in
                                let position = CellPosition(row: rowIdx, col: colIdx)
                                ExcelCellView(
                                    cell: cell,
                                    isSelected: selectedCells.contains(position)
                                )
                                .onTapGesture {
                                    handleCellTap(position: position, cell: cell)
                                }
                            }

                            // 填充剩余的空单元格
                            ForEach(row.count..<maxCols, id: \.self) { _ in
                                ExcelCellView(
                                    cell: MergedCell(type: .single("")),
                                    isSelected: false
                                )
                            }
                        }
                    }
                }
            }
        }
        .sheet(isPresented: $showDetailSheet) {
            if let selected = lastSelectedCell,
               selected.row < rows.count,
               selected.col < rows[selected.row].count {
                CellDetailView(
                    cell: rows[selected.row][selected.col],
                    cellReference: cellReference(row: selected.row, col: selected.col),
                    rowIndex: selected.row,
                    colIndex: selected.col,
                    onApplyOverride: { type in
                        onApplyOverride?(selected.row, selected.col, type)
                    }
                )
            }
        }
        .sheet(isPresented: $showBulkEditSheet) {
            BulkEditView(
                selectedCount: selectedCells.count,
                onApply: { type in
                    onApplyBulkOverride?(Array(selectedCells), type)
                    selectedCells.removeAll()
                }
            )
        }
    }

    private func handleCellTap(position: CellPosition, cell: MergedCell) {
        let isCommandPressed = NSApp.currentEvent?.modifierFlags.contains(.command) ?? false
        let isShiftPressed = NSApp.currentEvent?.modifierFlags.contains(.shift) ?? false

        if isShiftPressed, let last = lastSelectedCell {
            // Shift+点击：范围选择
            selectRange(from: last, to: position)
        } else if isCommandPressed {
            // Cmd+点击：添加/移除选择
            if selectedCells.contains(position) {
                selectedCells.remove(position)
            } else {
                selectedCells.insert(position)
            }
            lastSelectedCell = position
        } else {
            // 普通点击：单选，如果已有选择则打开批量编辑
            if selectedCells.isEmpty || selectedCells.count == 1 {
                selectedCells = [position]
                lastSelectedCell = position
                if !cell.sourceValues.isEmpty {
                    showDetailSheet = true
                }
            } else if selectedCells.contains(position) {
                // 点击已选中的单元格，打开批量编辑
                showBulkEditSheet = true
            } else {
                // 点击其他位置，清空并单选
                selectedCells = [position]
                lastSelectedCell = position
            }
        }
    }

    private func selectRange(from: CellPosition, to: CellPosition) {
        let minRow = min(from.row, to.row)
        let maxRow = max(from.row, to.row)
        let minCol = min(from.col, to.col)
        let maxCol = max(from.col, to.col)

        for row in minRow...maxRow {
            for col in minCol...maxCol {
                selectedCells.insert(CellPosition(row: row, col: col))
            }
        }
        lastSelectedCell = to
    }

    private var columnHeaderRow: some View {
        HStack(spacing: 0) {
            // 左上角空白（行号和列头的交叉处）
            Rectangle()
                .fill(Color(NSColor.controlBackgroundColor))
                .frame(width: 40, height: 24)
                .overlay(
                    Rectangle()
                        .stroke(Color.gray.opacity(0.3), lineWidth: 0.5)
                )

            // 列头 A, B, C...
            ForEach(0..<maxCols, id: \.self) { colIdx in
                Text(columnLetters(colIdx))
                    .font(.system(size: 11))
                    .fontWeight(.medium)
                    .foregroundStyle(.secondary)
                    .frame(width: 100, height: 24)
                    .background(Color(NSColor.controlBackgroundColor))
                    .overlay(
                        Rectangle()
                            .stroke(Color.gray.opacity(0.3), lineWidth: 0.5)
                    )
            }
        }
    }

    private func rowNumberView(rowIdx: Int) -> some View {
        Text("\(rowIdx + 1)")
            .font(.system(size: 11))
            .fontWeight(.medium)
            .foregroundStyle(.secondary)
            .frame(width: 40, height: 24)
            .background(Color(NSColor.controlBackgroundColor))
            .overlay(
                Rectangle()
                    .stroke(Color.gray.opacity(0.3), lineWidth: 0.5)
            )
            .contentShape(Rectangle())
    }

    private func cellReference(row: Int, col: Int) -> String {
        "\(columnLetters(col))\(row + 1)"
    }

    private func columnLetters(_ col: Int) -> String {
        var result = ""
        var num = col
        repeat {
            result = String(UnicodeScalar(65 + (num % 26))!) + result
            num = num / 26 - 1
        } while num >= 0
        return result
    }
}

// MARK: - 批量编辑视图

struct BulkEditView: View {
    let selectedCount: Int
    let onApply: (CellOverrideType) -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var selectedType: CellOverrideType = .sum

    var body: some View {
        VStack(spacing: 20) {
            HStack {
                Text("批量修正")
                    .font(.headline)

                Spacer()

                Button("取消") {
                    dismiss()
                }
            }

            Divider()

            VStack(spacing: 12) {
                Text("已选择 \(selectedCount) 个单元格")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)

                Text("选择要应用的类型:")
                    .font(.subheadline)
            }

            Picker("类型", selection: $selectedType) {
                ForEach(CellOverrideType.allCases, id: \.self) { type in
                    Text(type.displayName).tag(type)
                }
            }
            .pickerStyle(.segmented)
            .frame(width: 250)

            VStack(alignment: .leading, spacing: 8) {
                ForEach(CellOverrideType.allCases, id: \.self) { type in
                    HStack {
                        Text("• \(type.displayName)")
                            .fontWeight(.medium)
                        Text("-\(type.description)")
                            .foregroundStyle(.secondary)
                    }
                    .font(.caption)
                }
            }

            Spacer()

            HStack {
                Button("取消") {
                    dismiss()
                }
                .keyboardShortcut(.escape)

                Button("应用") {
                    onApply(selectedType)
                    dismiss()
                }
                .keyboardShortcut(.return)
                .buttonStyle(.borderedProminent)
            }
        }
        .padding()
        .frame(width: 350, height: 320)
    }
}

// MARK: - Excel 单元格视图

struct ExcelCellView: View {
    let cell: MergedCell
    let sourceValues: [String: String]
    let isSelected: Bool

    init(cell: MergedCell, isSelected: Bool) {
        self.cell = cell
        self.sourceValues = cell.sourceValues
        self.isSelected = isSelected
    }

    var body: some View {
        Text(cell.displayValue)
            .font(.system(size: 12))
            .lineLimit(1)
            .truncationMode(.tail)
            .frame(width: 100, height: 24, alignment: alignment)
            .padding(.horizontal, 4)
            .background(backgroundColor)
            .foregroundStyle(foregroundStyle)
            .overlay(
                Rectangle()
                    .stroke(Color.gray.opacity(0.2), lineWidth: 0.5)
            )
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
            return Color.accentColor.opacity(0.15)
        }
        switch cell.type {
        case .sum:
            return Color.blue.opacity(0.05)
        case .mixed:
            return Color.yellow.opacity(0.08)
        default:
            return Color.white
        }
    }

    private var foregroundStyle: some ShapeStyle {
        switch cell.type {
        case .mixed:
            return Color.orange
        case .sum:
            return Color.blue
        default:
            return Color.primary
        }
    }
}

// MARK: - 单元格详情视图

struct CellDetailView: View {
    let cell: MergedCell
    let cellReference: String
    let rowIndex: Int
    let colIndex: Int
    var onApplyOverride: ((CellOverrideType) -> Void)?

    @Environment(\.dismiss) private var dismiss
    @State private var selectedOverride: CellOverrideType?
    @State private var showSavedConfirmation = false

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                Text("单元格 \(cellReference)")
                    .font(.headline)

                if cell.isOverridden {
                    Image(systemName: "pencil.circle.fill")
                        .foregroundStyle(.orange)
                        .help("用户已修正")
                }

                Spacer()

                Button("关闭") {
                    dismiss()
                }
            }

            Divider()

            // 显示聚合信息
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    Text("显示值:")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)

                    Text(cell.displayValue)
                        .font(.title3)
                        .fontWeight(.semibold)
                }

                Spacer()

                VStack(alignment: .trailing, spacing: 4) {
                    CellTypeBadge(type: cell.type)

                    if cell.isOverridden {
                        Text("用户修正")
                            .font(.caption2)
                            .foregroundStyle(.orange)
                    }
                }
            }

            Divider()

            // 类型修正区域
            VStack(alignment: .leading, spacing: 12) {
                Text("修正识别类型")
                    .font(.subheadline)
                    .fontWeight(.medium)

                Text("如果自动识别不正确，可以手动指定单元格类型:")
                    .font(.caption)
                    .foregroundStyle(.secondary)

                Picker("类型", selection: $selectedOverride) {
                    Text("自动识别").tag(nil as CellOverrideType?)

                    ForEach(CellOverrideType.allCases, id: \.self) { type in
                        HStack {
                            Text(type.displayName)
                            Spacer()
                            Text(type.description)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                        .tag(type as CellOverrideType?)
                    }
                }
                .pickerStyle(.radioGroup)

                HStack {
                    Spacer()

                    Button("应用修正") {
                        if let type = selectedOverride {
                            onApplyOverride?(type)
                            showSavedConfirmation = true
                        }
                    }
                    .disabled(selectedOverride == nil)
                    .buttonStyle(.borderedProminent)
                }

                if showSavedConfirmation {
                    HStack {
                        Spacer()

                        Label("修正已应用", systemImage: "checkmark.circle.fill")
                            .font(.caption)
                            .foregroundStyle(.green)

                        Spacer()
                    }
                    .padding(.vertical, 4)
                }
            }

            Divider()

            // 来源详情
            Text("来源详情 (\(cell.sourceValues.count) 个文件)")
                .font(.subheadline)

            List(Array(cell.sourceValues.sorted(by: { $0.key < $1.key }).enumerated()), id: \.offset) { _, item in
                HStack {
                    Text(item.key)
                        .font(.caption)
                        .lineLimit(1)

                    Spacer()

                    Text(item.value)
                        .font(.system(.body, design: .monospaced))
                }
            }
        }
        .padding()
        .frame(width: 500, height: 600)
    }
}

// MARK: - 单元格类型标签

struct CellTypeBadge: View {
    let type: MergedCell.CellType

    var body: some View {
        Text(badgeText)
            .font(.caption)
            .padding(.horizontal, 8)
            .padding(.vertical, 2)
            .background(badgeColor.opacity(0.15))
            .foregroundStyle(badgeColor)
            .cornerRadius(4)
    }

    private var badgeText: String {
        switch type {
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

// MARK: - 拖拽支持

struct DropDelegateView: DropDelegate {
    @ObservedObject var viewModel: AppViewModel

    func performDrop(info: DropInfo) -> Bool {
        let items = info.itemProviders(for: [.fileURL])

        for item in items {
            item.loadItem(forTypeIdentifier: "public.file-url", options: nil) { data, error in
                guard let data = data as? Data,
                      let url = URL(dataRepresentation: data, relativeTo: nil) else { return }

                let path = url.path
                let ext = url.pathExtension.lowercased()

                if ext == "xlsx" || ext == "xls" {
                    DispatchQueue.main.async {
                        viewModel.loadFiles(at: [path], append: true)
                    }
                }
            }
        }

        return true
    }

    func validateDrop(info: DropInfo) -> Bool {
        return info.hasItemsConforming(to: [.fileURL])
    }
}
