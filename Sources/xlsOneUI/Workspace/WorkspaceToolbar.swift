import SwiftUI
import xlsOneCore
import xlsOneLicense

/// Main toolbar for the xlsOne workspace.
struct WorkspaceToolbar: View {
    @EnvironmentObject var viewModel: AppViewModel

    var body: some View {
        let presentation = viewModel.toolbarPresentation
        let hasWorkspace = !viewModel.selectedFilePaths.isEmpty

        HStack(spacing: XSpacing.lg) {
            if hasWorkspace {
                HStack(spacing: XSpacing.xs) {
                    utilityButton(
                        label: LocLabel("追加", systemImage: "plus"),
                        help: LocaleManager.loc("向当前批次追加文件")
                    ) {
                        viewModel.showAddFileDialog()
                    }
                    .disabled(!presentation.appendEnabled)

                    Divider()
                        .frame(height: 18)

                    utilityButton(
                        label: LocLabel("刷新", systemImage: "arrow.clockwise"),
                        help: LocaleManager.loc("重新读取当前文件并刷新汇总结果")
                    ) {
                        viewModel.reloadFiles()
                    }
                    .disabled(viewModel.selectedFilePaths.isEmpty)

                    utilityButton(
                        label: LocLabel("清空", systemImage: "xmark"),
                        help: LocaleManager.loc("清空当前工作区，不影响原始 Excel 文件")
                    ) {
                        viewModel.closeAllFiles()
                    }
                    .disabled(viewModel.selectedFilePaths.isEmpty)
                }
                .padding(XSpacing.xs)
                .background(XColor.surface)
                .clipShape(RoundedRectangle(cornerRadius: XRadius.lg, style: .continuous))
            }

            Spacer()

            HStack(spacing: XSpacing.md) {
                Button {
                    viewModel.showHelpPanel = true
                } label: {
                    Image(systemName: "questionmark.circle")
                        .font(XFont.toolbarButton)
                }
                .buttonStyle(.xLink)
                .help(LocaleManager.loc("快速参考指南"))

                LicenseStatusBadge()

                if hasWorkspace {
                    Button {
                        viewModel.exportResult()
                    } label: {
                        LocLabel("导出 XLSX", systemImage: "square.and.arrow.up")
                    }
                    .buttonStyle(.xPrimary)
                    .help(viewModel.canExport ? LocaleManager.loc("导出同构汇总 Excel") : LocaleManager.loc("当前没有可导出的汇总结果"))
                    .disabled(!viewModel.canExport)
                }
            }
        }
        .padding(.horizontal)
        .padding(.vertical, XSpacing.md)
        .background(XColor.background)
    }

    private func utilityButton(
        label: some View,
        help: String,
        action: @escaping () -> Void
    ) -> some View {
        Button(action: action) {
            label
        }
        .buttonStyle(.xSecondary)
        .help(help)
    }
}

#if DEBUG
struct WorkspaceToolbar_Previews: PreviewProvider {
    static var previews: some View {
        WorkspaceToolbar()
            .environmentObject(AppViewModel())
            .environmentObject(LicenseManager.shared)
    }
}
#endif
