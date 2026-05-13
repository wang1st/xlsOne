import SwiftUI
import xlsOneCore

/// 当前同构结构的调整记忆视图
struct SchemaManagerView: View {
    @EnvironmentObject private var viewModel: AppViewModel
    @Environment(\.dismiss) private var dismiss
    @ObservedObject private var localeManager = LocaleManager.shared
    private let onClose: (() -> Void)?

    init(onClose: (() -> Void)? = nil) {
        self.onClose = onClose
    }

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Text(LocaleManager.loc("当前调整记忆"))
                    .font(.headline)

                Spacer()

                Button(LocaleManager.loc("关闭"), action: close)
            }
            .padding()

            Divider()

            Group {
                if let schema = viewModel.currentAdjustmentMemory {
                    AdjustmentMemoryDetailView(schema: schema)
                } else {
                    emptyState
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)

            Divider()

            HStack {
                Button(LocaleManager.loc("导入"), action: viewModel.showImportAdjustmentMemoryDialog)
                    .disabled(!viewModel.canManageAdjustmentMemory)

                Spacer()

                Button(LocaleManager.loc("导出")) {
                    viewModel.exportCurrentAdjustmentMemory()
                }
                .disabled(viewModel.currentAdjustmentMemory == nil)

                Button(LocaleManager.loc("清除当前调整记忆"), role: .destructive) {
                    viewModel.clearCurrentAdjustmentMemory()
                }
                .disabled(viewModel.currentAdjustmentMemory == nil)
            }
            .padding()
        }
        .frame(width: 600, height: 400)
    }

    private var emptyState: some View {
        VStack(spacing: 16) {
            Image(systemName: "doc.text.magnifyingglass")
                .font(.system(size: 48))
                .foregroundStyle(.secondary)

            Text(LocaleManager.loc("当前同构结构暂无调整记忆"))
                .font(.title3)
                .foregroundStyle(.secondary)

            Text(LocaleManager.loc("在汇总结果中修正单元格后，系统会为当前同构结构记住这些调整；也可以导入同构结构的调整记忆。"))
                .font(.caption)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 360)
        }
    }

    private func close() {
        if let onClose {
            onClose()
        } else {
            dismiss()
        }
    }
}

private struct AdjustmentMemoryDetailView: View {
    let schema: MergeSchema

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                detailRow(LocaleManager.loc("名称"), schema.name)
                detailRow(LocaleManager.loc("更新"), Self.dateFormatter.string(from: schema.updatedAt))
                detailRow(LocaleManager.loc("工作表"), sheetSummary)
                detailRow(LocaleManager.loc("修正"), "\(schema.cellOverrides.count) 处")

                Divider()

                if schema.cellOverrides.isEmpty {
                    Text(LocaleManager.loc("当前调整记忆没有单元格修正。"))
                        .foregroundStyle(.secondary)
                } else {
                    VStack(alignment: .leading, spacing: 8) {
                        ForEach(Array(schema.cellOverrides.enumerated()), id: \.offset) { _, override in
                            Text("\(sheetName(for: override)) \(WorkspaceDiagnostics.cellReference(row: override.rowIndex, col: override.colIndex)) -> \(override.cellType.displayName)")
                                .font(.system(.body, design: .monospaced))
                        }
                    }
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding()
        }
    }

    private var sheetSummary: String {
        if let workbookFingerprint = schema.workbookFingerprint,
           !workbookFingerprint.sheetFingerprints.isEmpty {
            return workbookFingerprint.sheetNames.joined(separator: ", ")
        }
        return schema.fingerprint.sheetName.isEmpty ? LocaleManager.loc("当前同构结构") : schema.fingerprint.sheetName
    }

    private func sheetName(for override: CellTypeOverride) -> String {
        guard let sheetName = override.sheetName, !sheetName.isEmpty else {
            return LocaleManager.loc("全部")
        }
        return sheetName
    }

    private func detailRow(_ title: String, _ value: String) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: 12) {
            Text(title)
                .foregroundStyle(.secondary)
                .frame(width: 56, alignment: .trailing)
            Text(value)
                .textSelection(.enabled)
        }
    }

    private static let dateFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateStyle = .medium
        formatter.timeStyle = .short
        return formatter
    }()
}
