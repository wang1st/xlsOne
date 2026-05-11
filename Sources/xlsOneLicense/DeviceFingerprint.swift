import Foundation
import IOKit
import CryptoKit

/// Generates a stable device fingerprint for license binding.
/// Uses MAC address hash — survives reboots, doesn't change with network.
enum DeviceFingerprint {

    static func generate() -> String {
        // Primary: platform serial number (most stable)
        if let serial = platformSerialNumber(), !serial.isEmpty {
            return sha256(serial + ".xlsone.device")
        }

        // Fallback: MAC address of primary network interface
        if let mac = primaryMACAddress() {
            return sha256(mac + ".xlsone.device")
        }

        // Last resort: UUID stored in keychain (persists across launches)
        return sha256(UUID().uuidString + ".xlsone.device")
    }

    // MARK: - Private

    private static func platformSerialNumber() -> String? {
        let service = IOServiceGetMatchingService(
            kIOMainPortDefault,
            IOServiceMatching("IOPlatformExpertDevice")
        )
        defer { IOObjectRelease(service) }

        if service == 0 { return nil }

        guard let serial = IORegistryEntryCreateCFProperty(
            service, "IOPlatformSerialNumber" as CFString,
            kCFAllocatorDefault, 0
        )?.takeRetainedValue() as? String else { return nil }

        return serial.trimmingCharacters(in: .whitespaces)
    }

    private static func primaryMACAddress() -> String? {
        var ifaddr: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&ifaddr) == 0, let first = ifaddr else { return nil }
        defer { freeifaddrs(ifaddr) }

        var ptr = first
        while true {
            let flags = Int32(ptr.pointee.ifa_flags)
            let isUp = (flags & IFF_UP) == IFF_UP
            let isLoopback = (flags & IFF_LOOPBACK) == IFF_LOOPBACK
            let isRunning = (flags & IFF_RUNNING) == IFF_RUNNING

            if isUp && !isLoopback && isRunning {
                let name = String(cString: ptr.pointee.ifa_name)
                // Skip virtual interfaces
                if name.hasPrefix("utun") || name.hasPrefix("llw") ||
                   name.hasPrefix("awdl") || name.hasPrefix("bridge") {
                    ptr = ptr.pointee.ifa_next
                    continue
                }

                let addr = ptr.pointee.ifa_addr.pointee
                if addr.sa_family == UInt8(AF_LINK) {
                    let data = ptr.pointee.ifa_addr.withMemoryRebound(
                        to: sockaddr_dl.self, capacity: 1
                    ) { ptr -> [UInt8]? in
                        let dl = ptr.pointee
                        if dl.sdl_alen == 0 { return nil }
                        let base = UnsafeRawPointer(ptr)
                            .advanced(by: Int(dl.sdl_nlen + dl.sdl_alen))
                        return Array(UnsafeBufferPointer(
                            start: base.assumingMemoryBound(to: UInt8.self),
                            count: Int(dl.sdl_alen)
                        ))
                    }
                    if let bytes = data {
                        return bytes.map { String(format: "%02x", $0) }.joined(separator: ":")
                    }
                }
            }

            if ptr.pointee.ifa_next == nil { break }
            ptr = ptr.pointee.ifa_next
        }

        return nil
    }

    private static func sha256(_ input: String) -> String {
        let data = Data(input.utf8)
        let hash = SHA256.hash(data: data)
        return hash.compactMap { String(format: "%02x", $0) }.joined()
    }
}
