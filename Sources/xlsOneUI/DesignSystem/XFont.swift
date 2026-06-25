import SwiftUI

/// Typography tokens for xlsOne.
///
/// All fonts use the system typeface to respect macOS Dynamic Type and
/// accessibility settings.
enum XFont {
    static let windowTitle: Font = .system(size: 28, weight: .semibold)
    static let panelTitle: Font = .system(size: 18, weight: .semibold)
    static let sectionTitle: Font = .system(size: 15, weight: .semibold)

    static let toolbarButton: Font = .system(size: 13, weight: .medium)
    static let body: Font = .system(size: 13, weight: .regular)
    static let callout: Font = .system(size: 12, weight: .regular)
    static let caption: Font = .system(size: 11, weight: .regular)
    static let caption2: Font = .system(size: 10, weight: .regular)

    static let gridData: Font = .system(size: 12, weight: .regular)
    static let gridHeader: Font = .system(size: 12, weight: .medium)
    static let rowNumber: Font = .system(size: 11, weight: .medium)

    static let monospacedData: Font = .system(size: 14, weight: .semibold, design: .monospaced)
    static let monospacedInput: Font = .system(.body, design: .monospaced)
}
