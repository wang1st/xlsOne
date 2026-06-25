import SwiftUI

/// Empty workspace placeholder shown when no files have been imported.
struct WorkspaceEmptyState: View {
    @EnvironmentObject var viewModel: AppViewModel
    let isDropTargeted: Bool

    var body: some View {
        ZStack {
            XColor.background
                .ignoresSafeArea()

            VStack(spacing: XSpacing.xl) {
                dropZoneCard
                    .frame(maxWidth: 560, maxHeight: 400)

                HStack(spacing: XSpacing.sm) {
                    Image(systemName: "command")
                        .font(XFont.caption)
                    Text(LocaleManager.loc("也可以按 ⌘O 选择文件"))
                        .font(XFont.caption)
                }
                .foregroundColor(XColor.tertiaryLabel)
            }
            .padding(XSpacing.xxl)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .animation(.easeInOut(duration: 0.18), value: isDropTargeted)
    }

    // MARK: - Drop Zone Card

    private var dropZoneCard: some View {
        ZStack {
            RoundedRectangle(cornerRadius: XRadius.xl, style: .continuous)
                .fill(XColor.surface)
                .overlay(
                    RoundedRectangle(cornerRadius: XRadius.xl, style: .continuous)
                        .stroke(borderColor, lineWidth: isDropTargeted ? 2 : 1)
                )
                .xShadow(isDropTargeted ? XShadow.dropZone : XShadow.card)

            VStack(spacing: XSpacing.xl) {
                brandIllustration

                VStack(spacing: XSpacing.sm) {
                    Text(titleText)
                        .font(XFont.windowTitle)
                        .foregroundColor(XColor.primaryLabel)

                    Text(subtitleText)
                        .font(.subheadline)
                        .foregroundColor(XColor.secondaryLabel)
                        .multilineTextAlignment(.center)
                }

                Button {
                    viewModel.showOpenFileDialog()
                } label: {
                    Label(LocaleManager.loc("选择文件"), systemImage: "folder.badge.plus")
                }
                .buttonStyle(.xPrimary)
                .controlSize(.large)
                .keyboardShortcut("o", modifiers: .command)
            }
            .padding(.horizontal, 48)
            .padding(.vertical, 40)
        }
    }

    private var borderColor: Color {
        isDropTargeted
            ? XColor.dropZoneBorder
            : XColor.border
    }

    private var titleText: String {
        isDropTargeted
            ? LocaleManager.loc("松手即可导入")
            : LocaleManager.loc("拖入 Excel 文件")
    }

    private var subtitleText: String {
        isDropTargeted
            ? LocaleManager.loc("支持多个 .xlsx / .xls")
            : LocaleManager.loc("支持多个 .xlsx / .xls，自动识别表头与可汇总列")
    }

    // MARK: - Brand Illustration

    /// Code-drawn illustration showing multiple sheets merging into one.
    /// Replace with a custom SVG/PNG asset when brand artwork is ready.
    private var brandIllustration: some View {
        ZStack {
            // Background sheets (source workbooks)
            sourceSheet(width: 110, height: 86, x: -32, y: -8, opacity: 0.35)
            sourceSheet(width: 118, height: 92, x: 28, y: -4, opacity: 0.45)

            // Foreground sheet (merged result)
            RoundedRectangle(cornerRadius: XRadius.lg, style: .continuous)
                .fill(XColor.surface)
                .frame(width: 132, height: 100)
                .overlay(
                    RoundedRectangle(cornerRadius: XRadius.lg, style: .continuous)
                        .stroke(
                            isDropTargeted ? XColor.accent.opacity(0.35) : XColor.border,
                            lineWidth: 1.2
                        )
                )
                .overlay(alignment: .topLeading) {
                    VStack(spacing: 7) {
                        RoundedRectangle(cornerRadius: XRadius.sm, style: .continuous)
                            .fill(XColor.accent.opacity(0.18))
                            .frame(height: 9)

                        ForEach(0..<3, id: \.self) { _ in
                            HStack(spacing: 5) {
                                ForEach(0..<3, id: \.self) { _ in
                                    RoundedRectangle(cornerRadius: 3, style: .continuous)
                                        .fill(XColor.secondaryLabel.opacity(0.12))
                                        .frame(width: 30, height: 12)
                                }
                            }
                        }
                    }
                    .padding(12)
                }

            // Merge arrow
            Image(systemName: "arrow.down.circle.fill")
                .font(.system(size: 22))
                .foregroundColor(XColor.accent)
                .background(XColor.surface)
                .clipShape(Circle())
                .offset(y: 52)
                .opacity(isDropTargeted ? 1 : 0.8)
        }
        .frame(width: 180, height: 130)
    }

    private func sourceSheet(width: CGFloat, height: CGFloat, x: CGFloat, y: CGFloat, opacity: Double) -> some View {
        RoundedRectangle(cornerRadius: XRadius.lg, style: .continuous)
            .fill(XColor.surface)
            .frame(width: width, height: height)
            .overlay(
                RoundedRectangle(cornerRadius: XRadius.lg, style: .continuous)
                    .stroke(XColor.border, lineWidth: 1)
            )
            .overlay {
                VStack(spacing: 7) {
                    ForEach(0..<3, id: \.self) { _ in
                        Rectangle()
                            .fill(XColor.secondaryLabel.opacity(0.08))
                            .frame(height: 1)
                    }
                }
                .padding(.horizontal, 12)
            }
            .offset(x: x, y: y)
            .opacity(opacity)
    }
}

#if DEBUG
struct WorkspaceEmptyState_Previews: PreviewProvider {
    static var previews: some View {
        WorkspaceEmptyState(isDropTargeted: false)
            .environmentObject(AppViewModel())
    }
}
#endif
