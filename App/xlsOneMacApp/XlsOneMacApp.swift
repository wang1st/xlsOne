import AppKit
import SwiftUI
import xlsOneUI

@main
struct XlsOneMacApp: App {
    @NSApplicationDelegateAdaptor(WorkspaceAppDelegate.self) private var appDelegate

    init() {
        NSWindow.allowsAutomaticWindowTabbing = false
    }

    var body: some Scene {
        XlsOneWorkspaceScene()
    }
}
