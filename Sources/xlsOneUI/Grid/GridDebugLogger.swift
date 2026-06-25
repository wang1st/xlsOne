import Foundation

enum GridDebugLogger {
    private static let queue = DispatchQueue(label: "xlsone.grid-debug-logger")
    private static let fileURL = URL(fileURLWithPath: NSTemporaryDirectory())
        .appendingPathComponent("xlsone-grid-cursor.log")
    private static var didWriteSessionHeader = false
    private static let debugOptInEnvironmentKey = "XLSONE_CURSOR_DEBUG_LOG"

    static var logPath: String {
        fileURL.path
    }

    static var isEnabledForCurrentProcess: Bool {
        isEnabled(environment: ProcessInfo.processInfo.environment)
    }

    static func isEnabled(environment: [String: String]) -> Bool {
        if let override = environment[debugOptInEnvironmentKey] {
            return override == "1"
        }

        #if DEBUG
        return environment["XCTestConfigurationFilePath"] == nil
        #else
        return false
        #endif
    }

    static func log(_ message: String) {
        guard isEnabledForCurrentProcess else { return }

        queue.async {
            if !didWriteSessionHeader {
                didWriteSessionHeader = true
                append("=== session start pid=\(ProcessInfo.processInfo.processIdentifier) at \(timestamp()) ===")
            }

            append(message)
        }
    }

    private static func append(_ line: String) {
        let output = "[\(timestamp())] \(line)\n"
        let data = Data(output.utf8)

        if !FileManager.default.fileExists(atPath: fileURL.path) {
            FileManager.default.createFile(atPath: fileURL.path, contents: nil)
        }

        guard let handle = try? FileHandle(forWritingTo: fileURL) else {
            return
        }

        do {
            try handle.seekToEnd()
            try handle.write(contentsOf: data)
            try handle.close()
        } catch {
            try? handle.close()
        }
    }

    private static func timestamp() -> String {
        ISO8601DateFormatter().string(from: Date())
    }
}
