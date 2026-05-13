import Foundation
import SwiftUI

public final class LocaleManager: ObservableObject {
    public static let shared = LocaleManager()

    @Published public var currentLanguage: AppLanguage {
        didSet {
            UserDefaults.standard.set(currentLanguage.rawValue, forKey: "AppLanguage")
            applyToFoundation()
        }
    }

    public enum AppLanguage: String, CaseIterable, Identifiable {
        case system
        case english = "en"
        case chineseSimplified = "zh-Hans"
        case chineseTraditional = "zh-Hant"

        public var id: String { rawValue }

        public var displayName: String {
            switch self {
            case .system: return "跟随系统"
            case .english: return "English"
            case .chineseSimplified: return "简体中文"
            case .chineseTraditional: return "繁體中文"
            }
        }

        public var localeIdentifier: String? {
            switch self {
            case .system: return nil
            case .english: return "en"
            case .chineseSimplified: return "zh-Hans"
            case .chineseTraditional: return "zh-Hant"
            }
        }

        public func isChineseLike() -> Bool {
            switch self {
            case .system:
                return Locale.preferredLanguages.first?.hasPrefix("zh") ?? false
            case .chineseSimplified, .chineseTraditional:
                return true
            case .english:
                return false
            }
        }
    }

    private init() {
        let stored = UserDefaults.standard.string(forKey: "AppLanguage") ?? ""
        currentLanguage = AppLanguage(rawValue: stored) ?? .system
        applyToFoundation()
    }

    public func applyToFoundation() {
        guard let id = currentLanguage.localeIdentifier else {
            UserDefaults.standard.removeObject(forKey: "AppleLanguages")
            UserDefaults.standard.synchronize()
            return
        }
        UserDefaults.standard.set([id], forKey: "AppleLanguages")
        UserDefaults.standard.synchronize()
    }

    public var swiftUILocale: Locale {
        if let id = currentLanguage.localeIdentifier {
            return Locale(identifier: id)
        }
        return .current
    }

    public static func loc(_ key: String) -> String {
        Bundle.module.localizedString(forKey: key, value: key, table: nil)
    }
}
