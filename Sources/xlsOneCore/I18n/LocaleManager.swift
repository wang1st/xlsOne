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

    private var translations: [String: [String: String]] = [:]

    private init() {
        let stored = UserDefaults.standard.string(forKey: "AppLanguage") ?? ""
        currentLanguage = AppLanguage(rawValue: stored) ?? .system
        applyToFoundation()
        loadTranslations()
    }

    private func loadTranslations() {
        guard let url = Bundle.module.url(forResource: "Localizable", withExtension: "xcstrings"),
              let data = try? Data(contentsOf: url),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let strings = json["strings"] as? [String: Any] else {
            return
        }

        for (key, value) in strings {
            guard let dict = value as? [String: Any],
                  let localizations = dict["localizations"] as? [String: Any] else { continue }
            
            var langDict: [String: String] = [:]
            for (lang, langData) in localizations {
                guard let langDictData = langData as? [String: Any],
                      let stringUnit = langDictData["stringUnit"] as? [String: Any],
                      let val = stringUnit["value"] as? String else { continue }
                langDict[lang] = val
            }
            translations[key] = langDict
        }
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
        let lang = LocaleManager.shared.currentLanguage.localeIdentifier ?? (Locale.preferredLanguages.first?.hasPrefix("zh") == true ? "zh-Hans" : "en")
        
        if let langDict = LocaleManager.shared.translations[key] {
            if let text = langDict[lang] { return text }
            if lang.hasPrefix("zh-Hant"), let text = langDict["zh-Hant"] { return text }
            if lang.hasPrefix("zh"), let text = langDict["zh-Hans"] { return text }
            if lang.hasPrefix("en"), let text = langDict["en"] { return text }
            if let text = langDict["en"] { return text }
            if let text = langDict["zh-Hans"] { return text }
        }
        
        return key
    }
}