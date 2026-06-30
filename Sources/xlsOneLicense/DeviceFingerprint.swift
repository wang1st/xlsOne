import Foundation
import CryptoKit

/// Generates a stable device fingerprint for license binding.
/// Uses a Keychain-stored UUID — survives reboots, survives Keychain reset
/// only if the user explicitly removes the entry.
enum DeviceFingerprint {

    static func generate() -> String {
        // Primary: Keychain-stored UUID (App Store compatible, no IOKit needed)
        if let existing = Keychain.load(key: "com.xlsone.deviceUUID") {
            return sha256(existing + ".xlsone.device")
        }

        // First launch: generate and persist a new UUID
        let uuid = UUID().uuidString
        _ = Keychain.save(key: "com.xlsone.deviceUUID", data: uuid)
        return sha256(uuid + ".xlsone.device")
    }

    // MARK: - Private

    private static func sha256(_ input: String) -> String {
        let data = Data(input.utf8)
        let hash = SHA256.hash(data: data)
        return hash.compactMap { String(format: "%02x", $0) }.joined()
    }
}
