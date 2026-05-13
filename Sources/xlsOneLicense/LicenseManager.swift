import Foundation
#if os(macOS)
import AppKit
#endif

// MARK: - License Manager

/// Manages software license activation, verification, and offline support.
/// Communicates with the xlsOne activation API for online validation.
public final class LicenseManager: ObservableObject {

    public static let shared = LicenseManager()
    public static var isAppStoreDistribution: Bool {
        Bundle.main.object(forInfoDictionaryKey: "XLSONEDistributionChannel") as? String == "app-store"
    }

    // MARK: - Published State

    @Published public private(set) var licenseState: LicenseState = .unactivated
    @Published public private(set) var plan: LicensePlan = .unknown
    @Published public private(set) var isVerifying = false
    @Published public var showActivationSheet = false

    // MARK: - Constants

    private enum API {
        /// Primary endpoint (Cloudflare Workers — global)
        static let primary = "https://api.xlsone.com"
        /// Fallback endpoint (Aliyun FC — China mainland)
        static let fallback = "https://"
        /// Request timeout
        static let timeout: TimeInterval = 3.0
    }

    private enum Storage {
        static let tokenKey = "com.xlsone.license.token"
        static let deviceIDKey = "com.xlsone.license.deviceID"
        static let offlineLicenseKey = "com.xlsone.license.offline"
        static let trialStartKey = "com.xlsone.license.trialStart"
    }

    private static let trialDurationDays = 14

    // MARK: - Initialization

    private init() {
        if Self.isAppStoreDistribution {
            licenseState = .activated
            plan = .appStore
        } else {
            loadPersistedState()
        }
    }

    // MARK: - Public API

    /// Start a free trial. Returns the remaining days.
    public func startTrial() -> Int {
        if Self.isAppStoreDistribution {
            licenseState = .activated
            plan = .appStore
            return Int.max
        }

        let now = Date()
        UserDefaults.standard.set(now, forKey: Storage.trialStartKey)
        let remaining = Self.trialDurationDays
        licenseState = .trial(remainingDays: remaining)
        return remaining
    }

    /// Check trial status. Returns remaining days or -1 if trial expired/not started.
    public func checkTrialStatus() -> Int {
        guard let start = UserDefaults.standard.object(forKey: Storage.trialStartKey) as? Date else {
            return -1
        }
        let elapsed = Calendar.current.dateComponents([.day], from: start, to: Date()).day ?? 0
        let remaining = Self.trialDurationDays - elapsed
        if remaining > 0 {
            return remaining
        }
        return -1
    }

    /// Import an offline license file (JWT token).
    @discardableResult
    @MainActor
    public func importOfflineLicenseFile() -> ActivationResult? {
        guard !Self.isAppStoreDistribution else {
            return .success(plan: .appStore, expiresAt: nil)
        }

#if os(macOS)
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.data, .json]
        panel.allowsMultipleSelection = false
        panel.message = "选择授权文件"

        guard Self.runCenteredModal(panel) == .OK, let url = panel.url else { return nil }

        guard let token = try? String(contentsOf: url, encoding: .utf8).trimmingCharacters(in: .whitespacesAndNewlines),
              !token.isEmpty
        else {
            DispatchQueue.main.async {
                self.licenseState = .unactivated
            }
            return .failure(.invalidLicenseFile)
        }

        let deviceID = getOrCreateDeviceID()
        guard let payload = decodeTokenPayload(token) else {
            DispatchQueue.main.async {
                self.licenseState = .unactivated
            }
            return .failure(.invalidLicenseFile)
        }

        guard payload.dev == deviceID else {
            return .failure(.licenseDeviceMismatch)
        }

        guard isTokenValidLocally(payload, deviceID: deviceID) else {
            return .failure(.invalidLicenseFile)
        }

        let plan = LicensePlan(rawValue: payload.plan) ?? .unknown
        saveToken(token)
        UserDefaults.standard.set(token, forKey: Storage.offlineLicenseKey)
        DispatchQueue.main.async {
            self.licenseState = .activated
            self.plan = plan
        }
        return .success(plan: plan, expiresAt: nil)
#else
        return .failure(.serverError("当前平台不支持导入授权文件"))
#endif
    }

    /// Activate with an activation key. Returns the result.
    public func activate(key: String) async -> ActivationResult {
        if Self.isAppStoreDistribution {
            await MainActor.run {
                self.licenseState = .activated
                self.plan = .appStore
            }
            return .success(plan: .appStore, expiresAt: nil)
        }

        let deviceID = getOrCreateDeviceID()
        let normalizedKey = key.uppercased().trimmingCharacters(in: .whitespaces)

        guard KeyFormat.isValid(normalizedKey) else {
            return .failure(.invalidKeyFormat)
        }

        let body: [String: String] = [
            "key": normalizedKey,
            "device_id": deviceID,
            "device_name": Host.current().localizedName ?? "Unknown Mac"
        ]

        // Try primary endpoint first
        if let result = await postActivation(to: API.primary, body: body) {
            return result
        }

        // Fallback to China endpoint
        if let result = await postActivation(to: API.fallback, body: body) {
            return result
        }

        // Allow offline activation attempt from stored offline license
        if let offlineResult = tryOfflineActivation(key: normalizedKey, deviceID: deviceID) {
            return offlineResult
        }

        return .failure(.networkError)
    }

    /// Verify current license validity. Called on app launch.
    public func verifyOnLaunch() async {
        if Self.isAppStoreDistribution {
            await MainActor.run {
                self.licenseState = .activated
                self.plan = .appStore
                self.showActivationSheet = false
            }
            return
        }

        await MainActor.run { isVerifying = true }
        defer { Task { @MainActor in isVerifying = false } }

        guard let token = loadToken() else {
            await setState(.unactivated)
            return
        }

        let deviceID = getOrCreateDeviceID()

        // Try online verification
        if let result = await postVerify(to: API.primary, token: token, deviceID: deviceID) {
            await handleVerifyResult(result)
            return
        }

        // Fallback endpoint
        if let result = await postVerify(to: API.fallback, token: token, deviceID: deviceID) {
            await handleVerifyResult(result)
            return
        }

        // Offline validation — check token locally
        if let payload = decodeTokenPayload(token), isTokenValidLocally(payload, deviceID: deviceID) {
            await setState(.activated)
            await setPlan(LicensePlan(rawValue: payload.plan) ?? .unknown)
            return
        }

        // Token invalid, try offline license file
        if let offlineToken = loadOfflineLicense(), let payload = decodeTokenPayload(offlineToken),
           isTokenValidLocally(payload, deviceID: deviceID) {
            await setState(.activated)
            await setPlan(LicensePlan(rawValue: payload.plan) ?? .unknown)
            return
        }

        // Grace period: if token was valid recently (within 7 days), allow
        if let lastValid = UserDefaults.standard.object(forKey: "com.xlsone.license.lastValid") as? Date,
           Date().timeIntervalSince(lastValid) < 7 * 24 * 60 * 60 {
            await setState(.gracePeriod)
            return
        }

        await setState(.expired)
    }

    /// Refresh subscription token. Called periodically.
    public func refreshIfNeeded() async {
        guard !Self.isAppStoreDistribution else { return }

        guard case .activated = licenseState,
              let token = loadToken(),
              let payload = decodeTokenPayload(token),
              payload.exp > 0, // lifetime licenses don't need refresh
              payload.exp < Int(Date().timeIntervalSince1970) + 7 * 24 * 60 * 60 // refresh if expiring within 7 days
        else { return }

        let deviceID = getOrCreateDeviceID()

        let body: [String: String] = ["token": token, "device_id": deviceID]

        if let data = await postJSON(to: "\(API.primary)/api/refresh", body: body),
           let result = try? JSONDecoder().decode(RefreshResponse.self, from: data),
           result.valid {
            if let token = result.token { saveToken(token) }
        }
    }

    /// Reset license (for user logout / key change)
    public func reset() {
        guard !Self.isAppStoreDistribution else {
            licenseState = .activated
            plan = .appStore
            showActivationSheet = false
            return
        }

        Keychain.delete(key: Storage.tokenKey)
        UserDefaults.standard.removeObject(forKey: Storage.offlineLicenseKey)
        licenseState = .unactivated
        plan = .unknown
    }

    // MARK: - Private — Network

    private func postActivation(to base: String, body: [String: String]) async -> ActivationResult? {
        guard let data = await postJSON(to: "\(base)/api/activate", body: body) else {
            return nil
        }

        guard let result = try? JSONDecoder().decode(ActivateResponse.self, from: data) else {
            if let error = try? JSONDecoder().decode(APIError.self, from: data) {
                return .failure(mapAPIError(error))
            }
            return nil
        }

        saveToken(result.license_token)

        let plan = LicensePlan(rawValue: result.plan) ?? .personalYearly
        Task { @MainActor in
            self.licenseState = .activated
            self.plan = plan
            UserDefaults.standard.set(Date(), forKey: "com.xlsone.license.lastValid")
        }

        return .success(plan: plan, expiresAt: result.expires_at)
    }

    private func postVerify(to base: String, token: String, deviceID: String) async -> VerifyResponse? {
        let body: [String: String] = ["token": token, "device_id": deviceID]
        guard let data = await postJSON(to: "\(base)/api/verify", body: body) else {
            return nil
        }
        return try? JSONDecoder().decode(VerifyResponse.self, from: data)
    }

    private func handleVerifyResult(_ result: VerifyResponse) async {
        if result.valid {
            await setState(.activated)
            await setPlan(LicensePlan(rawValue: result.plan) ?? .unknown)
            UserDefaults.standard.set(Date(), forKey: "com.xlsone.license.lastValid")
        } else if result.expired == true {
            await setState(.expired)
        } else {
            await setState(.unactivated)
        }
    }

    private func postJSON(to urlString: String, body: [String: String]) async -> Data? {
        guard let url = URL(string: urlString) else { return nil }

        var request = URLRequest(url: url, timeoutInterval: API.timeout)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = try? JSONSerialization.data(withJSONObject: body)

        do {
            let (data, response) = try await URLSession.shared.data(for: request)
            guard let http = response as? HTTPURLResponse, (200...299).contains(http.statusCode) else {
                return nil
            }
            return data
        } catch {
            return nil
        }
    }

    // MARK: - Private — Offline

    private func tryOfflineActivation(key: String, deviceID: String) -> ActivationResult? {
        // Offline activation requires a pre-generated .license file
        guard let licenseData = loadOfflineLicense() else { return nil }

        // Verify the offline license matches the key and device
        guard let payload = decodeTokenPayload(licenseData),
              payload.sub == key,
              payload.dev == deviceID,
              isTokenValidLocally(payload, deviceID: deviceID)
        else { return nil }

        saveToken(licenseData)
        let plan = LicensePlan(rawValue: payload.plan) ?? .personalLifetime
        Task { @MainActor in
            self.licenseState = .activated
            self.plan = plan
        }
        return .success(plan: plan, expiresAt: payload.exp > 0
            ? Date(timeIntervalSince1970: TimeInterval(payload.exp)).ISO8601Format() : nil)
    }

    private func isTokenValidLocally(_ payload: TokenPayload, deviceID: String) -> Bool {
        guard payload.dev == deviceID else { return false }
        // Lifetime token
        if payload.exp == 0 { return true }
        // Subscription token with expiry
        return payload.exp > Int(Date().timeIntervalSince1970)
    }

    // MARK: - Private — Storage

    private func loadPersistedState() {
        if let token = loadToken(), let payload = decodeTokenPayload(token),
           isTokenValidLocally(payload, deviceID: getOrCreateDeviceID()) {
            licenseState = .activated
            plan = LicensePlan(rawValue: payload.plan) ?? .unknown
            return
        }
        let remaining = checkTrialStatus()
        if remaining > 0 {
            licenseState = .trial(remainingDays: remaining)
        }
    }

    private func getOrCreateDeviceID() -> String {
        if let existing = Keychain.load(key: Storage.deviceIDKey) {
            return existing
        }
        let id = DeviceFingerprint.generate()
        _ = Keychain.save(key: Storage.deviceIDKey, data: id)
        return id
    }

    private func loadToken() -> String? {
        Keychain.load(key: Storage.tokenKey)
    }

    private func saveToken(_ token: String) {
        _ = Keychain.save(key: Storage.tokenKey, data: token)
    }

    private func loadOfflineLicense() -> String? {
        UserDefaults.standard.string(forKey: Storage.offlineLicenseKey)
    }

    private func decodeTokenPayload(_ token: String) -> TokenPayload? {
        let parts = token.split(separator: ".")
        guard parts.count >= 2 else { return nil }
        // Base64URL decode the payload (second segment)
        var base64 = String(parts[1])
            .replacingOccurrences(of: "-", with: "+")
            .replacingOccurrences(of: "_", with: "/")
        while base64.count % 4 != 0 { base64 += "=" }
        guard let data = Data(base64Encoded: base64),
              let payload = try? JSONDecoder().decode(TokenPayload.self, from: data)
        else { return nil }
        return payload
    }

    @MainActor private func setState(_ state: LicenseState) { self.licenseState = state }
    @MainActor private func setPlan(_ plan: LicensePlan) { self.plan = plan }

#if os(macOS)
    @MainActor
    private static func runCenteredModal(_ panel: NSSavePanel) -> NSApplication.ModalResponse {
        panel.contentView?.layoutSubtreeIfNeeded()
        if let owner = NSApp.keyWindow ?? NSApp.mainWindow ?? NSApp.windows.first(where: { $0.isVisible }) {
            let ownerFrame = owner.frame
            let panelFrame = panel.frame
            panel.setFrameOrigin(NSPoint(
                x: ownerFrame.midX - panelFrame.width / 2,
                y: ownerFrame.midY - panelFrame.height / 2
            ))
        } else {
            panel.center()
        }
        return panel.runModal()
    }
#endif

    private func mapAPIError(_ error: APIError) -> ActivationError {
        switch error.error {
        case "KEY_NOT_FOUND":   return .keyNotFound
        case "KEY_REVOKED":     return .keyRevoked
        case "DEVICE_LIMIT":    return .deviceLimit
        case "RATE_LIMITED":    return .rateLimited
        default:                return .serverError(error.message ?? "未知错误")
        }
    }
}

// MARK: - Types

public enum LicenseState: Equatable {
    case unactivated
    case activated
    case expired
    case gracePeriod     // Offline, token not verified recently but within grace window
    case trial(remainingDays: Int)  // Free trial with N days remaining
}

public enum LicensePlan: String {
    case appStore = "app_store"
    case personalYearly = "personal_yearly"
    case personalLifetime = "personal_lifetime"
    case enterprise10 = "enterprise_10"
    case enterprise25 = "enterprise_25"
    case enterpriseUnlimited = "enterprise_unlimited"
    case unknown = "unknown"

    public var displayName: String {
        switch self {
        case .appStore:           return String(localized: "App Store")
        case .personalYearly:      return String(localized: "个人年度订阅")
        case .personalLifetime:    return String(localized: "个人永久授权")
        case .enterprise10:        return String(localized: "企业版 (10台)")
        case .enterprise25:        return String(localized: "企业版 (25台)")
        case .enterpriseUnlimited: return String(localized: "企业版 (无限)")
        case .unknown:             return String(localized: "未知")
        }
    }
}

public enum ActivationResult: Equatable {
    case success(plan: LicensePlan, expiresAt: String?)
    case failure(ActivationError)
}

public enum ActivationError: Error, Equatable {
    case invalidKeyFormat
    case invalidLicenseFile
    case licenseDeviceMismatch
    case keyNotFound
    case keyRevoked
    case deviceLimit
    case networkError
    case rateLimited
    case serverError(String)

    public var localizedDescription: String {
        switch self {
        case .invalidKeyFormat: return String(localized: "激活码格式不正确")
        case .invalidLicenseFile: return String(localized: "授权文件无效或已过期")
        case .licenseDeviceMismatch: return String(localized: "授权文件与当前设备不匹配")
        case .keyNotFound:      return String(localized: "激活码不存在")
        case .keyRevoked:       return String(localized: "激活码已被吊销")
        case .deviceLimit:      return String(localized: "已达到最大设备数限制")
        case .networkError:     return String(localized: "无法连接激活服务器，请检查网络")
        case .rateLimited:      return String(localized: "请求过于频繁，请稍后再试")
        case .serverError(let m): return String(localized: "服务器错误: \(m)")
        }
    }
}

// MARK: - API Response Types (internal)

struct ActivateResponse: Decodable {
    let license_token: String
    let plan: String
    let expires_at: String?
    let device_id: String
}

struct VerifyResponse: Decodable {
    let valid: Bool
    let plan: String
    let expires_at: String?
    let expired: Bool?

    enum CodingKeys: String, CodingKey {
        case valid, plan, expires_at
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        valid = try container.decode(Bool.self, forKey: .valid)
        plan = (try? container.decode(String.self, forKey: .plan)) ?? "unknown"
        expires_at = try? container.decodeIfPresent(String.self, forKey: .expires_at)
        expired = nil // derived from error code if valid=false
    }
}

struct RefreshResponse: Decodable {
    let valid: Bool
    let token: String?
    let refreshed: Bool?
}

struct APIError: Decodable {
    let error: String
    let message: String?
}

struct TokenPayload: Decodable {
    let sub: String   // key_id
    let dev: String   // device_id
    let plan: String
    let iat: Int
    let exp: Int      // 0 = lifetime
}

// MARK: - Key Format Validation

enum KeyFormat {
    static let pattern = try! NSRegularExpression(pattern: "^[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}$")

    static func isValid(_ key: String) -> Bool {
        pattern.firstMatch(in: key, range: NSRange(key.startIndex..., in: key)) != nil
    }
}
