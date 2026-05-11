import SwiftUI

// MARK: - License Activation View

/// Modal sheet shown when the app needs activation.
public struct LicenseActivationView: View {
    @ObservedObject var licenseManager: LicenseManager
    @State private var keyInput = ""
    @State private var isActivating = false
    @State private var errorMessage: String?
    @State private var showOfflineInfo = false

    public init(licenseManager: LicenseManager = .shared) {
        self.licenseManager = licenseManager
    }

    public var body: some View {
        VStack(spacing: 0) {
            // Icon + Title
            VStack(spacing: 12) {
                Image(systemName: "key.fill")
                    .font(.system(size: 40))
                    .foregroundColor(.accentColor)

                Text("激活 表表归一")
                    .font(.title2)
                    .fontWeight(.semibold)

                if licenseManager.licenseState == .expired {
                    Text("您的许可证已过期，请续费获取新的激活码")
                        .font(.subheadline)
                        .foregroundColor(.secondary)
                        .multilineTextAlignment(.center)
                } else {
                    Text("输入激活码以解锁全部功能")
                        .font(.subheadline)
                        .foregroundColor(.secondary)
                }
            }
            .padding(.top, 32)
            .padding(.bottom, 24)

            // Activation Key Input
            VStack(alignment: .leading, spacing: 8) {
                Text("激活码")
                    .font(.callout)
                    .fontWeight(.medium)

                TextField("XXXX-XXXX-XXXX", text: $keyInput)
                    .textFieldStyle(.plain)
                    .font(.system(.body, design: .monospaced))
                    .padding(12)
                    .background(Color(nsColor: .controlBackgroundColor))
                    .clipShape(RoundedRectangle(cornerRadius: 8))
                    .overlay(
                        RoundedRectangle(cornerRadius: 8)
                            .stroke(Color.secondary.opacity(0.3), lineWidth: 1)
                    )
                    .onChange(of: keyInput) { newValue in
                        // Auto-insert dashes and uppercase
                        var cleaned = newValue.uppercased().replacingOccurrences(of: "-", with: "")
                        if cleaned.count > 12 { cleaned = String(cleaned.prefix(12)) }

                        var formatted = ""
                        for (i, ch) in cleaned.enumerated() {
                            if i > 0 && i % 4 == 0 { formatted += "-" }
                            formatted.append(ch)
                        }
                        if formatted != newValue {
                            keyInput = formatted
                        }
                    }
            }

            // Error message
            if let error = errorMessage {
                HStack(spacing: 6) {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .font(.caption)
                    Text(error)
                        .font(.caption)
                }
                .foregroundColor(.red)
                .padding(.top, 8)
            }

            // Spacer
            Spacer().frame(height: 20)

            // Activate Button
            Button(action: activate) {
                HStack(spacing: 8) {
                    if isActivating {
                        ProgressView()
                            .scaleEffect(0.8)
                    }
                    Text(isActivating ? "验证中..." : "激活")
                        .fontWeight(.semibold)
                }
                .frame(maxWidth: .infinity)
                .padding(.vertical, 12)
                .background(validInput ? Color.accentColor : Color.gray.opacity(0.3))
                .foregroundColor(validInput ? .white : .secondary)
                .clipShape(RoundedRectangle(cornerRadius: 8))
            }
            .disabled(!validInput || isActivating)
            .buttonStyle(.plain)

            Spacer().frame(height: 12)

            // Trial & Purchase
            HStack(spacing: 24) {
                Button("免费试用 14 天") {
                    // TODO: Implement trial mode
                }
                .buttonStyle(.link)

                Button("购买激活码 →") {
                    if let url = URL(string: "https://z-pulse.cn") {
                        NSWorkspace.shared.open(url)
                    }
                }
                .buttonStyle(.link)
            }
            .font(.subheadline)

            Spacer().frame(height: 12)

            // Offline activation
            Button {
                showOfflineInfo.toggle()
            } label: {
                HStack(spacing: 4) {
                    Image(systemName: "wifi.slash")
                    Text("离线激活")
                }
                .font(.caption)
                .foregroundColor(.secondary)
            }
            .buttonStyle(.plain)

            if showOfflineInfo {
                VStack(alignment: .leading, spacing: 4) {
                    Text("离线激活方式：")
                        .font(.caption)
                        .fontWeight(.medium)
                    Text("1. 在联网电脑上访问 z-pulse.cn/offline")
                        .font(.caption)
                        .foregroundColor(.secondary)
                    Text("2. 输入购买邮箱和本机设备码")
                        .font(.caption)
                        .foregroundColor(.secondary)
                    Text("3. 下载授权文件并导入本程序")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
                .padding(12)
                .background(Color(nsColor: .controlBackgroundColor))
                .clipShape(RoundedRectangle(cornerRadius: 8))
            }

            Spacer()
        }
        .padding(.horizontal, 48)
        .frame(width: 420, height: 520)
        .background(Color(nsColor: .windowBackgroundColor))
    }

    private var validInput: Bool {
        keyInput.count == 14 // XXXX-XXXX-XXXX
    }

    private func activate() {
        guard validInput else { return }
        isActivating = true
        errorMessage = nil

        Task {
            let result = await licenseManager.activate(key: keyInput)
            await MainActor.run {
                isActivating = false
                switch result {
                case .success:
                    break // sheet dismisses automatically via state change
                case .failure(let error):
                    errorMessage = error.localizedDescription
                }
            }
        }
    }
}

// MARK: - License Status Badge

/// Small indicator showing license status in toolbar
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
                .font(.caption)
                .foregroundColor(.secondary)
        }
    }

    private var color: Color {
        switch licenseManager.licenseState {
        case .activated:   return .green
        case .expired,
             .unactivated:  return .red
        case .gracePeriod: return .orange
        }
    }

    private var label: String {
        switch licenseManager.licenseState {
        case .activated:   return licenseManager.plan.displayName
        case .unactivated: return "未激活"
        case .expired:     return "已过期"
        case .gracePeriod: return "离线模式"
        }
    }
}
