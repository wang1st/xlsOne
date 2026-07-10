import SwiftUI
import xlsOneCore
import xlsOneLicense

// MARK: - License Activation View

/// Modal sheet shown when the app needs activation.
public struct LicenseActivationView: View {
    @ObservedObject var licenseManager: LicenseManager
    @ObservedObject private var localeManager = LocaleManager.shared

    @State private var keyParts: [String] = ["", "", "", ""]
    @State private var isActivating = false
    @State private var errorMessage: String?
    @State private var successMessage: String?
    @State private var showOfflineInfo = false
    @FocusState private var focusedField: ActivationField?

    public init(licenseManager: LicenseManager = .shared) {
        self.licenseManager = licenseManager
    }

    public var body: some View {
        HStack(spacing: 0) {
            brandPanel
            formPanel
        }
        .frame(minWidth: 640, idealWidth: 720, maxWidth: .infinity, minHeight: 480, idealHeight: 520, maxHeight: .infinity)
        .background(XColor.background)
    }

    // MARK: - Brand Panel

    private var brandPanel: some View {
        ZStack {
            XColor.accent.opacity(0.08)

            VStack(spacing: XSpacing.lg) {
                Spacer()

                Image(systemName: "tablecells.badge.ellipsis")
                    .font(.system(size: 56))
                    .foregroundColor(XColor.accent)

                VStack(spacing: XSpacing.sm) {
                    Text(LocaleManager.loc("表表归一"))
                        .font(XFont.panelTitle)
                        .foregroundColor(XColor.primaryLabel)

                    Text(LocaleManager.loc("多张同格式 Excel 报表一键汇总"))
                        .font(XFont.body)
                        .foregroundColor(XColor.secondaryLabel)
                        .multilineTextAlignment(.center)
                }

                Spacer()

                HStack(spacing: XSpacing.sm) {
                    featurePill(icon: "bolt", text: LocaleManager.loc("快速"))
                    featurePill(icon: "checkmark.shield", text: LocaleManager.loc("安全"))
                    featurePill(icon: "macwindow", text: LocaleManager.loc("原生"))
                }
            }
            .padding(XSpacing.xxl)
        }
        .frame(minWidth: 260, idealWidth: 300, maxWidth: 320)
    }

    private func featurePill(icon: String, text: String) -> some View {
        HStack(spacing: XSpacing.xs) {
            Image(systemName: icon)
                .font(XFont.caption)
            Text(text)
                .font(XFont.caption)
        }
        .foregroundColor(XColor.secondaryLabel)
        .padding(.horizontal, XSpacing.md)
        .padding(.vertical, XSpacing.xs)
        .background(XColor.surface)
        .clipShape(Capsule())
    }

    // MARK: - Form Panel

    private var formPanel: some View {
        VStack(alignment: .leading, spacing: 0) {
            headerSection
                .padding(.bottom, XSpacing.xl)

            activationCodeSection
                .padding(.bottom, XSpacing.lg)

            messageArea
                .padding(.bottom, XSpacing.md)

            Spacer().frame(height: XSpacing.lg)

            activateButton
                .padding(.bottom, XSpacing.md)

            trialAndPurchaseRow
                .padding(.bottom, XSpacing.xl)

            offlineActivationSection

            Spacer()
        }
        .padding(XSpacing.xxl)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var headerSection: some View {
        VStack(alignment: .leading, spacing: XSpacing.sm) {
            Text(LocaleManager.loc("激活 表表归一"))
                .font(XFont.panelTitle)
                .foregroundColor(XColor.primaryLabel)

            if licenseManager.licenseState == .expired {
                Text(LocaleManager.loc("您的许可证已过期，请续费获取新的激活码"))
                    .font(XFont.body)
                    .foregroundColor(XColor.secondaryLabel)
            } else {
                Text(LocaleManager.loc("输入激活码以解锁全部功能"))
                    .font(XFont.body)
                    .foregroundColor(XColor.secondaryLabel)
            }
        }
    }

    // MARK: - Activation Code

    private var activationCodeSection: some View {
        VStack(alignment: .leading, spacing: XSpacing.sm) {
            Text(LocaleManager.loc("激活码"))
                .font(XFont.callout)
                .fontWeight(.medium)
                .foregroundColor(XColor.primaryLabel)

            HStack(spacing: XSpacing.sm) {
                ForEach(0..<4, id: \.self) { index in
                    activationCodeField(index: index)
                    if index < 3 {
                        Text("-")
                            .font(XFont.monospacedData)
                            .foregroundColor(XColor.secondaryLabel)
                    }
                }
            }
        }
    }

    @ViewBuilder
    private func activationCodeField(index: Int) -> some View {
        let field = TextField("", text: $keyParts[index])
            .textFieldStyle(.plain)
            .font(XFont.monospacedInput)
            .multilineTextAlignment(.center)
            .frame(width: 64, height: 40)
            .background(XColor.surface)
            .clipShape(RoundedRectangle(cornerRadius: XRadius.md, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: XRadius.md, style: .continuous)
                    .stroke(focusedField == .field(index) ? XColor.accent : XColor.border, lineWidth: 1)
            )
            .focused($focusedField, equals: .field(index))
            .onChange(of: keyParts[index]) { newValue in
                handleKeyPartChange(index: index, newValue: newValue)
            }

        if #available(macOS 14.0, *) {
            field.onKeyPress(.delete) {
                handleBackspace(index: index)
                return .handled
            }
        } else {
            field
        }
    }

    private func handleKeyPartChange(index: Int, newValue: String) {
        errorMessage = nil
        successMessage = nil

        let cleaned = newValue.uppercased().filter { $0.isLetter || $0.isNumber }
        if cleaned.count > 4 {
            var remaining = cleaned
            keyParts[index] = String(remaining.prefix(4))
            remaining.removeFirst(4)

            for i in (index + 1)..<4 {
                guard !remaining.isEmpty else { break }
                let part = String(remaining.prefix(4))
                keyParts[i] = part
                remaining.removeFirst(min(4, remaining.count))
            }

            if index < 3 {
                focusedField = .field(min(index + 1, 3))
            }
        } else {
            keyParts[index] = cleaned
            if cleaned.count == 4 && index < 3 {
                focusedField = .field(index + 1)
            }
        }
    }

    private func handleBackspace(index: Int) {
        if keyParts[index].isEmpty && index > 0 {
            focusedField = .field(index - 1)
        }
    }

    // MARK: - Messages

    private var messageArea: some View {
        VStack(alignment: .leading, spacing: XSpacing.sm) {
            if let error = errorMessage {
                HStack(spacing: XSpacing.sm) {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .font(XFont.caption)
                    Text(error)
                        .font(XFont.caption)
                    Spacer()
                }
                .foregroundColor(XColor.error)
            } else if let success = successMessage {
                HStack(spacing: XSpacing.sm) {
                    Image(systemName: "checkmark.circle.fill")
                        .font(XFont.caption)
                    Text(success)
                        .font(XFont.caption)
                    Spacer()
                }
                .foregroundColor(XColor.success)
            } else {
                HStack(spacing: XSpacing.sm) {
                    Text(" ")
                        .font(XFont.caption)
                }
                .foregroundColor(.clear)
            }
        }
        .frame(minHeight: 28, alignment: .top)
    }

    // MARK: - Actions

    private var activateButton: some View {
        Button(action: activate) {
            HStack(spacing: XSpacing.sm) {
                if isActivating {
                    ProgressView()
                        .scaleEffect(0.8)
                }
                Text(isActivating ? LocaleManager.loc("验证中...") : LocaleManager.loc("激活"))
                    .fontWeight(.semibold)
            }
            .frame(maxWidth: .infinity)
            .padding(.vertical, XSpacing.md)
        }
        .buttonStyle(.xPrimary)
        .disabled(!validInput || isActivating)
    }

    private var trialAndPurchaseRow: some View {
        HStack(spacing: XSpacing.xl) {
            Button(LocaleManager.loc("免费试用 14 天")) {
                let remaining = licenseManager.startTrial()
                if remaining > 0 {
                    errorMessage = nil
                    successMessage = nil
                    licenseManager.showActivationSheet = false
                }
            }
            .buttonStyle(.xLink)

            Spacer()

            Button(LocaleManager.loc("购买激活码 →")) {
                if let url = URL(string: "https://z-pulse.cn/xlsone/") {
                    NSWorkspace.shared.open(url)
                }
            }
            .buttonStyle(.xLink)
        }
    }

    private var offlineActivationSection: some View {
        VStack(alignment: .leading, spacing: XSpacing.md) {
            Button {
                showOfflineInfo.toggle()
            } label: {
                HStack(spacing: XSpacing.xs) {
                    Image(systemName: "wifi.slash")
                    Text(LocaleManager.loc("离线激活"))
                    Spacer()
                    Image(systemName: showOfflineInfo ? "chevron.up" : "chevron.down")
                        .font(XFont.caption)
                }
                .font(XFont.callout)
                .foregroundColor(XColor.secondaryLabel)
            }
            .buttonStyle(.xLink)

            if showOfflineInfo {
                VStack(alignment: .leading, spacing: XSpacing.sm) {
                    OfflineStep(number: 1, text: LocaleManager.loc("在联网电脑上访问 z-pulse.cn/xlsone/offline"))
                    OfflineStep(number: 2, text: LocaleManager.loc("输入购买邮箱和本机设备码"))
                    OfflineStep(number: 3, text: LocaleManager.loc("下载授权文件并导入本程序"))

                    Button {
                        importLicenseFile()
                    } label: {
                        Label(LocaleManager.loc("导入授权文件..."), systemImage: "doc.badge.plus")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.xBordered)
                    .padding(.top, XSpacing.sm)
                }
                .padding(XSpacing.md)
                .background(XColor.surface)
                .clipShape(RoundedRectangle(cornerRadius: XRadius.md, style: .continuous))
            }
        }
    }

    // MARK: - Helpers

    private var validInput: Bool {
        keyParts.allSatisfy { $0.count == 4 }
    }

    private var fullKey: String {
        keyParts.joined(separator: "-")
    }

    private func activate() {
        guard validInput else { return }
        isActivating = true
        errorMessage = nil
        successMessage = nil

        Task {
            let result = await licenseManager.activate(key: fullKey)
            await MainActor.run {
                isActivating = false
                switch result {
                case .success:
                    licenseManager.showActivationSheet = false
                case .failure(let error):
                    errorMessage = error.localizedDescription
                }
            }
        }
    }

    private func importLicenseFile() {
        errorMessage = nil
        successMessage = nil

        guard let result = licenseManager.importOfflineLicenseFile() else { return }
        switch result {
        case .success(let plan, _):
            successMessage = String(format: LocaleManager.loc("授权文件已导入：%@"), plan.displayName)
            licenseManager.showActivationSheet = false
        case .failure(let error):
            errorMessage = error.localizedDescription
        }
    }
}

// MARK: - Focus State

private enum ActivationField: Hashable {
    case field(Int)
}

// MARK: - Offline Step

private struct OfflineStep: View {
    let number: Int
    let text: String

    var body: some View {
        HStack(alignment: .top, spacing: XSpacing.sm) {
            Text("\(number)")
                .font(XFont.caption)
                .fontWeight(.semibold)
                .foregroundColor(.white)
                .frame(width: 18, height: 18)
                .background(XColor.accent)
                .clipShape(Circle())

            Text(text)
                .font(XFont.caption)
                .foregroundColor(XColor.secondaryLabel)
                .fixedSize(horizontal: false, vertical: true)

            Spacer()
        }
    }
}

// MARK: - License Status Badge

/// Small indicator showing license status in toolbar.
public struct LicenseStatusBadge: View {
    @ObservedObject var licenseManager: LicenseManager

    public init(licenseManager: LicenseManager = .shared) {
        self.licenseManager = licenseManager
    }

    public var body: some View {
        HStack(spacing: 4) {
            Circle()
                .fill(color)
                .frame(width: 6, height: 6)
            Text(label)
                .font(XFont.caption)
                .foregroundColor(XColor.secondaryLabel)
        }
    }

    private var color: Color {
        switch licenseManager.licenseState {
        case .activated:   return XColor.success
        case .expired,
             .unactivated:  return XColor.error
        case .gracePeriod: return XColor.warning
        case .trial:       return XColor.info
        }
    }

    private var label: String {
        switch licenseManager.licenseState {
        case .activated:   return licenseManager.plan.displayName
        case .unactivated: return LocaleManager.loc("未激活")
        case .expired:     return LocaleManager.loc("已过期")
        case .gracePeriod: return LocaleManager.loc("离线模式")
        case .trial(let remaining): return String(format: LocaleManager.loc("试用 %d 天"), remaining)
        }
    }
}

#if DEBUG
struct LicenseActivationView_Previews: PreviewProvider {
    static var previews: some View {
        LicenseActivationView()
    }
}
#endif
