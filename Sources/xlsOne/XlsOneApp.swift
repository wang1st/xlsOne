import AppKit
import SwiftUI
import xlsOneCore
import xlsOneUI

@main
struct XlsOneApp: App {
    @NSApplicationDelegateAdaptor(WorkspaceAppDelegate.self) private var appDelegate

    init() {
        NSWindow.allowsAutomaticWindowTabbing = false
        LocaleManager.shared.applyToFoundation()
    }

    var body: some Scene {
        XlsOneWorkspaceScene()
    }
}

