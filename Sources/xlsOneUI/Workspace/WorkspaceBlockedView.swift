import SwiftUI
import xlsOneCore

/// View shown when validation blocks the workspace from being ready.
struct WorkspaceBlockedView: View {
    let report: WorkbookValidationReport

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: XSpacing.xl) {
                HStack(alignment: .top, spacing: XSpacing.md) {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .foregroundColor(XColor.warning)
                        .font(.title2)

                    VStack(alignment: .leading, spacing: XSpacing.xs) {
                        Text(LocaleManager.loc("没有可参与汇总的同构工作表"))
                            .font(XFont.sectionTitle)
                            .foregroundColor(XColor.primaryLabel)

                        Text(LocaleManager.loc("系统已忽略尾部空白行列后重试校验，但当前仍没有任何 sheet 能在所有文件间对齐。"))
                            .font(XFont.body)
                            .foregroundColor(XColor.secondaryLabel)
                    }
                }

                validationSummary(report)

                if report.skippedSheetCount > 0 {
                    skippedSheetList(report)
                }

                fileParticipationList(report.files)
            }
            .padding(XSpacing.xxl)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .background(XColor.background)
    }

    // MARK: - Summary

    private func validationSummary(_ report: WorkbookValidationReport) -> some View {
        HStack(spacing: XSpacing.md) {
            statCard(title: LocaleManager.loc("参与文件"), value: "\(report.includedFiles.count)", tint: XColor.success)
            statCard(title: LocaleManager.loc("阻断文件"), value: "\(report.blockedFiles.count)", tint: XColor.error)
            statCard(title: LocaleManager.loc("警告文件"), value: "\(report.warningFiles.count)", tint: XColor.warning)
            if report.skippedSheetCount > 0 {
                statCard(title: LocaleManager.loc("跳过工作表"), value: "\(report.skippedSheetCount)", tint: XColor.warning)
            }
        }
    }

    private func statCard(title: String, value: String, tint: Color) -> some View {
        VStack(alignment: .leading, spacing: XSpacing.xs) {
            Text(title)
                .font(XFont.caption)
                .foregroundColor(XColor.secondaryLabel)
            Text(value)
                .font(XFont.sectionTitle)
                .fontWeight(.semibold)
                .foregroundColor(tint)
        }
        .padding(XSpacing.md)
        .frame(maxWidth: 140, alignment: .leading)
        .background(tint.opacity(0.08))
        .clipShape(RoundedRectangle(cornerRadius: XRadius.lg, style: .continuous))
    }

    // MARK: - File List

    private func fileParticipationList(_ reports: [FileValidationReport]) -> some View {
        VStack(alignment: .leading, spacing: XSpacing.md) {
            Text(LocaleManager.loc("文件参与情况"))
                .font(XFont.sectionTitle)
                .foregroundColor(XColor.primaryLabel)

            ForEach(reports, id: \.filepath) { report in
                VStack(alignment: .leading, spacing: XSpacing.sm) {
                    HStack(spacing: XSpacing.md) {
                        statusDot(for: report.status)
                        Text(report.filename)
                            .font(XFont.callout)
                            .fontWeight(.medium)
                            .foregroundColor(XColor.primaryLabel)
                        Spacer()
                        Text(report.statusLabel)
                            .font(XFont.caption)
                            .foregroundColor(report.statusColor)
                    }

                    if !report.issues.isEmpty {
                        VStack(alignment: .leading, spacing: XSpacing.xs) {
                            ForEach(report.issues) { issue in
                                Text(issue.message)
                                    .font(XFont.caption)
                                    .foregroundColor(issue.severity == .blocking ? XColor.error : XColor.warning)
                            }
                        }
                    }
                }
                .padding(XSpacing.md)
                .background(XColor.surface)
                .clipShape(RoundedRectangle(cornerRadius: XRadius.lg, style: .continuous))
                .overlay(
                    RoundedRectangle(cornerRadius: XRadius.lg, style: .continuous)
                        .stroke(report.statusColor.opacity(0.15), lineWidth: 1)
                )
            }
        }
    }

    // MARK: - Skipped Sheets

    private func skippedSheetList(_ report: WorkbookValidationReport) -> some View {
        VStack(alignment: .leading, spacing: XSpacing.md) {
            Text(LocaleManager.loc("已跳过的工作表"))
                .font(XFont.sectionTitle)
                .foregroundColor(XColor.primaryLabel)

            ForEach(report.skippedSheetNames, id: \.self) { sheetName in
                if let consensus = WorkspaceDiagnostics.buildSkippedSheetConsensus(report: report, sheetName: sheetName) {
                    SkippedSheetConsensusCard(consensus: consensus)
                } else {
                    VStack(alignment: .leading, spacing: XSpacing.xs) {
                        HStack(spacing: XSpacing.md) {
                            Image(systemName: "exclamationmark.triangle.fill")
                                .foregroundColor(XColor.warning)
                            Text(sheetName)
                                .font(XFont.callout)
                                .fontWeight(.medium)
                            Spacer()
                            Text(LocaleManager.loc("不参与合并"))
                                .font(XFont.caption)
                                .foregroundColor(XColor.warning)
                        }

                        ForEach(report.skippedSheetIssues.filter { $0.sheetName == sheetName }) { issue in
                            Text(issue.message)
                                .font(XFont.caption)
                                .foregroundColor(XColor.warning)
                        }
                    }
                    .padding(XSpacing.md)
                    .background(XColor.warning.opacity(0.06))
                    .clipShape(RoundedRectangle(cornerRadius: XRadius.lg, style: .continuous))
                }
            }
        }
    }

    private func statusDot(for status: FileValidationStatus) -> some View {
        Circle()
            .fill(statusColor(for: status))
            .frame(width: 8, height: 8)
    }

    private func statusColor(for status: FileValidationStatus) -> Color {
        switch status {
        case .included:
            return XColor.success
        case .warning:
            return XColor.warning
        case .blocked:
            return XColor.error
        }
    }
}

private extension FileValidationReport {
    var statusColor: Color {
        switch status {
        case .included: return XColor.success
        case .warning: return XColor.warning
        case .blocked: return XColor.error
        }
    }

    var statusLabel: String {
        switch status {
        case .included: return LocaleManager.loc("参与合并")
        case .warning: return LocaleManager.loc("已跳过")
        case .blocked: return LocaleManager.loc("阻断")
        }
    }
}
