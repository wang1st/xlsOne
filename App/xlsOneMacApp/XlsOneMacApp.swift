import SwiftUI
import xlsOneUI

@main
struct XlsOneMacApp: App {
    @NSApplicationDelegateAdaptor(WorkspaceAppDelegate.self) private var appDelegate

    var body: some Scene {
        XlsOneWorkspaceScene()
    }
}
