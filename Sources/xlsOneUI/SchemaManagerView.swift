import SwiftUI
import xlsOneCore

/// 调整记忆管理视图
struct SchemaManagerView: View {
    @StateObject private var viewModel = SchemaManagerViewModel()
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Text("调整记忆")
                    .font(.headline)

                Spacer()

                Button("关闭") {
                    dismiss()
                }
            }
            .padding()

            Divider()

            if viewModel.isLoading {
                ProgressView()
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else if viewModel.schemas.isEmpty {
                VStack(spacing: 16) {
                    Image(systemName: "doc.text.magnifyingglass")
                        .font(.system(size: 48))
                        .foregroundStyle(.secondary)

                    Text("暂无已记住的调整")
                        .font(.title3)
                        .foregroundStyle(.secondary)

                    Text("系统会在后台记住你对同结构表格的调整，需要时也可以在这里查看或清理。")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                List {
                    ForEach(viewModel.schemas) { schema in
                        SchemaRowView(
                            schema: schema,
                            onDelete: {
                                Task {
                                    await viewModel.deleteSchema(id: schema.id)
                                }
                            },
                            onExport: {
                                viewModel.exportSchema(id: schema.id)
                            }
                        )
                    }
                }
            }

            Divider()

            HStack {
                Button("导入记忆...") {
                    viewModel.showImportPanel = true
                }

                Spacer()

                Text("\(viewModel.schemas.count) 组记忆")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .padding()
        }
        .frame(width: 600, height: 400)
        .task {
            await viewModel.loadSchemas()
        }
        .fileImporter(
            isPresented: $viewModel.showImportPanel,
            allowedContentTypes: [.json],
            allowsMultipleSelection: false
        ) { result in
            Task {
                await viewModel.importSchema(from: result)
            }
        }
        .alert("错误", isPresented: $viewModel.showError) {
            Button("确定", role: .cancel) {}
        } message: {
            Text(viewModel.errorMessage ?? "未知错误")
        }
    }
}

// MARK: - 记忆行视图

struct SchemaRowView: View {
    let schema: MergeSchema
    let onDelete: () -> Void
    let onExport: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    Text(schema.name)
                        .font(.headline)

                    HStack(spacing: 8) {
                        Label(schema.scopeSummary, systemImage: "rectangle.stack")
                        Text("•")
                        Label("\(schema.cellOverrides.count) 处已记住调整", systemImage: "pencil")
                        Text("•")
                        Label("沿用 \(schema.matchCount) 次", systemImage: "checkmark.circle")
                    }
                    .font(.caption)
                    .foregroundStyle(.secondary)
                }

                Spacer()

                Menu {
                    Button {
                        onExport()
                    } label: {
                        Label("导出", systemImage: "square.and.arrow.up")
                    }

                    Button(role: .destructive) {
                        onDelete()
                    } label: {
                        Label("删除", systemImage: "trash")
                    }
                } label: {
                    Image(systemName: "ellipsis.circle")
                        .font(.title3)
                }
            }

        }
        .padding(.vertical, 8)
    }
}

private extension MergeSchema {
    var scopeSummary: String {
        if let workbookFingerprint, !workbookFingerprint.sheetFingerprints.isEmpty {
            let sheetCount = workbookFingerprint.sheetFingerprints.count
            if sheetCount == 1, let sheetName = workbookFingerprint.sheetFingerprints.first?.sheetName {
                return sheetName
            }
            return "\(sheetCount) 张工作表"
        }

        return fingerprint.sheetName.isEmpty ? "较早记忆" : fingerprint.sheetName
    }
}

// MARK: - ViewModel

@MainActor
class SchemaManagerViewModel: ObservableObject {
    @Published var schemas: [MergeSchema] = []
    @Published var isLoading = false
    @Published var showError = false
    @Published var errorMessage: String?
    @Published var showImportPanel = false

    private let repository = SchemaRepository.shared

    func loadSchemas() async {
        isLoading = true
        defer { isLoading = false }

        do {
            schemas = try await repository.loadAllSchemas()
        } catch {
            errorMessage = "加载失败: \(error.localizedDescription)"
            showError = true
        }
    }

    func deleteSchema(id: UUID) async {
        do {
            try await repository.deleteSchema(id: id)
            schemas.removeAll { $0.id == id }
        } catch {
            errorMessage = "删除失败: \(error.localizedDescription)"
            showError = true
        }
    }

    func exportSchema(id: UUID) {
        Task {
            do {
                let data = try await repository.exportSchema(id: id)

                // 显示保存面板
                let panel = NSSavePanel()
                panel.nameFieldStringValue = "memory-\(id.uuidString.prefix(8)).json"
                panel.allowedContentTypes = [.json]

                if panel.runModal() == .OK, let url = panel.url {
                    try data.write(to: url)
                }
            } catch {
                errorMessage = "导出失败: \(error.localizedDescription)"
                showError = true
            }
        }
    }

    func importSchema(from result: Result<[URL], Error>) async {
        do {
            let urls = try result.get()
            guard let url = urls.first else { return }

            let data = try Data(contentsOf: url)
            _ = try await repository.importSchema(data: data)

            // 刷新列表
            await loadSchemas()
        } catch {
            errorMessage = "导入失败: \(error.localizedDescription)"
            showError = true
        }
    }
}
