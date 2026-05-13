import SwiftUI
import xlsOneCore

public struct LanguagePickerView: View {
    @ObservedObject private var localeManager = LocaleManager.shared
    @State private var selectedLanguage: LocaleManager.AppLanguage

    public init() {
        _selectedLanguage = State(initialValue: LocaleManager.shared.currentLanguage)
    }

    public var body: some View {
        Picker(selection: $selectedLanguage) {
            ForEach(LocaleManager.AppLanguage.allCases) { language in
                Text(language.displayName)
                    .tag(language)
            }
        } label: {
            Label(String(localized: "语言"), systemImage: "globe")
        }
        .pickerStyle(.menu)
        .onChange(of: selectedLanguage) { newLanguage in
            localeManager.currentLanguage = newLanguage
            localeManager.applyToFoundation()
            AlgorithmI18n.shared.reload()
        }
    }
}
