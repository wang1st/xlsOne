import SwiftUI
import xlsOneCore

public struct LocText: View {
    let key: String
    @ObservedObject private var localeManager = LocaleManager.shared

    public init(_ key: String) {
        self.key = key
    }

    public var body: some View {
        Text(verbatim: LocaleManager.loc(key))
    }
}

public struct LocLabel: View {
    let key: String
    let systemImage: String
    @ObservedObject private var localeManager = LocaleManager.shared

    public init(_ key: String, systemImage: String) {
        self.key = key
        self.systemImage = systemImage
    }

    public var body: some View {
        Label(
            title: { Text(verbatim: LocaleManager.loc(key)) },
            icon: { Image(systemName: systemImage) }
        )
    }
}
