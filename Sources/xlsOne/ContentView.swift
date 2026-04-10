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
        HStack(spacing: 16) {
            Button("打开") {
                viewModel.showOpenFileDialog()
            }
            .buttonStyle(.borderedProminent)

            Button("导出") {
                viewModel.exportResult()
            }
            .buttonStyle(.bordered)
            .disabled(viewModel.mergedResult == nil)

            Spacer()

            // 文件计数
            if !viewModel.files.isEmpty {
                Text("\(viewModel.files.count) 个文件")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Button("设置") {
                // 设置功能待实现
            }
            .buttonStyle(.borderless)
        }
        .padding()
    }

    private var sheetSelector: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 8) {
                Text("工作表:")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)

                ForEach(viewModel.availableSheets, id: \.self) { sheetName in
                    Button(sheetName) {
                        viewModel.switchToSheet(sheetName)
                    }
                    .buttonStyle(SheetButtonStyle(isSelected: viewModel.currentSheet == sheetName))
                }
            }
            .padding(.horizontal)
            .padding(.vertical, 8)
        }
        .background(Color(NSColor.controlBackgroundColor))
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
    }

    private var emptyView: some View {
        VStack(spacing: 16) {
            Image(systemName: "doc.text.magnifyingglass")
                .font(.system(size: 48))
                .foregroundStyle(.secondary)

            Text("拖拽 Excel 文件到此处")
                .font(.title3)

            Text("或使用 ⌘O 打开文件")
                .font(.caption)
                .foregroundStyle(.secondary)

            Button("打开文件") {
                viewModel.showOpenFileDialog()
            }
            .buttonStyle(.borderedProminent)
            .padding(.top)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(NSColor.controlBackgroundColor))
    }

    private var noDataView: some View {
        VStack {
            Text("无法读取数据")
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func spreadsheetView(result: MergedResult) -> some View {
        VStack(spacing: 0) {
            // 表头信息
            HStack {
                Text("工作表: \(result.sheetName)")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)

                Spacer()

                Text("来源: \(result.sourceFiles.count) 个文件")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .padding(.horizontal)
            .padding(.vertical, 8)

            // 表格
            SpreadsheetGridView(rows: result.rows)
        }
    }
}

// MARK: - 工作表按钮样式

struct SheetButtonStyle: ButtonStyle {
    let isSelected: Bool

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
            .background(isSelected ? Color.accentColor : Color(NSColor.controlBackgroundColor))
            .foregroundStyle(isSelected ? .white : .primary)
            .cornerRadius(4)
            .overlay(
                RoundedRectangle(cornerRadius: 4)
                    .stroke(isSelected ? Color.accentColor : Color.gray.opacity(0.3), lineWidth: 1)
            )
    }
}

// MARK: - 电子表格网格视图

struct SpreadsheetGridView: View {
    let rows: [[MergedCell]]
    @State private var selectedCell: (row: Int, col: Int)?
    @State private var showDetailSheet = false

    var body: some View {
        GeometryReader { geometry in
            ScrollView([.horizontal, .vertical]) {
                VStack(alignment: .leading, spacing: 0) {
                    ForEach(Array(rows.enumerated()), id: \.offset) { rowIdx, row in
                        HStack(spacing: 0) {
                            // 行号
                            Text("\(rowIdx + 1)")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                                .frame(width: 40, height: 28)
                                .background(Color(NSColor.controlBackgroundColor))
                                .border(Color.gray.opacity(0.2), width: 0.5)

                            // 单元格
                            ForEach(Array(row.enumerated()), id: \.offset) { colIdx, cell in
                                CellView(cell: cell, isSelected: selectedCell?.row == rowIdx && selectedCell?.col == colIdx)
                                    .onTapGesture {
                                        selectedCell = (rowIdx, colIdx)
                                        if !cell.sourceValues.isEmpty {
                                            showDetailSheet = true
                                        }
                                    }
                            }
                        }
                    }
                }
                .padding(1)
            }
        }
        .sheet(isPresented: $showDetailSheet) {
            if let selected = selectedCell,
               selected.row < rows.count,
               selected.col < rows[selected.row].count {
                CellDetailView(
                    cell: rows[selected.row][selected.col],
                    cellReference: cellReference(row: selected.row, col: selected.col)
                )
            }
        }
    }

    private func cellReference(row: Int, col: Int) -> String {
        let colLetters = columnLetters(col)
        return "\(colLetters)\(row + 1)"
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

// MARK: - 单元格视图

struct CellView: View {
    let cell: MergedCell
    let isSelected: Bool

    var body: some View {
        Text(cell.displayValue)
            .font(.system(size: 12, design: .monospaced))
            .frame(width: 100, height: 28, alignment: alignment)
            .padding(.horizontal, 4)
            .background(backgroundColor)
            .foregroundStyle(foregroundStyle)
            .border(isSelected ? Color.accentColor : Color.gray.opacity(0.2), width: isSelected ? 2 : 0.5)
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
            return Color.accentColor.opacity(0.1)
        }
        switch cell.type {
        case .sum:
            return Color.blue.opacity(0.05)
        case .mixed:
            return Color.gray.opacity(0.05)
        default:
            return Color.white
        }
    }

    private var foregroundStyle: some ShapeStyle {
        switch cell.type {
        case .mixed:
            return Color.secondary
        default:
            return Color.primary
        }
    }
}

// MARK: - 单元格详情视图

struct CellDetailView: View {
    let cell: MergedCell
    let cellReference: String
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                Text("单元格 \(cellReference)")
                    .font(.headline)

                Spacer()

                Button("关闭") {
                    dismiss()
                }
            }

            Divider()

            // 显示聚合信息
            HStack {
                Text("显示值:")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)

                Text(cell.displayValue)
                    .font(.title3)
                    .fontWeight(.semibold)

                Spacer()

                CellTypeBadge(type: cell.type)
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
        .frame(width: 400, height: 500)
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
            .background(badgeColor.opacity(0.2))
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
