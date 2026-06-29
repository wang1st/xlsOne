import SwiftUI
import xlsOneCore

/// Loading placeholder shown while the workbook is being validated.
struct WorkspaceLoadingView: View {
    let fileCount: Int

    var body: some View {
        VStack(spacing: XSpacing.lg) {
            ProgressView()
                .scaleEffect(1.4)

            Text(LocaleManager.loc("正在校验工作簿结构并准备汇总工作台…"))
                .font(XFont.body)
                .foregroundColor(XColor.secondaryLabel)

            Text(String(format: LocaleManager.loc("已选 %d 个文件"), fileCount))
                .font(XFont.caption)
                .foregroundColor(XColor.secondaryLabel)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(XColor.background)
    }
}

#if DEBUG
struct WorkspaceLoadingView_Previews: PreviewProvider {
    static var previews: some View {
        WorkspaceLoadingView(fileCount: 4)
    }
}
#endif
