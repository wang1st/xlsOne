import AppKit
import SwiftUI
import xlsOneCore
import xlsOneUI

@main
struct XlsOneApp: App {
    @NSApplicationDelegateAdaptor(WorkspaceAppDelegate.self) private var appDelegate
    @StateObject private var localeManager = LocaleManager.shared

    init() {
        NSWindow.allowsAutomaticWindowTabbing = false
        LocaleManager.shared.applyToFoundation()
    }

    var body: some Scene {
        XlsOneWorkspaceScene()
            .environment(\.locale, Locale(identifier: localeManager.currentLanguage.localeIdentifier ?? "zh-Hans"))
    }
}
