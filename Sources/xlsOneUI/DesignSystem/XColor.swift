import SwiftUI

/// Semantic color tokens for xlsOne.
///
/// All colors are resolved against the current `NSAppearance` so they adapt
/// to Light, Dark and High Contrast modes automatically. Never hardcode
/// `.white` or `.black` in UI code; use these tokens instead.
enum XColor {
    // MARK: - Background

    static var background: Color {
        Color(NSColor.windowBackgroundColor)
    }

    static var surface: Color {
        Color(NSColor.controlBackgroundColor)
    }

    static var elevatedSurface: Color {
        Color(NSColor.controlBackgroundColor)
    }

    static var headerBackground: Color {
        Color(NSColor.headerColor)
    }

    // MARK: - Text

    static var primaryLabel: Color {
        Color.primary
    }

    static var secondaryLabel: Color {
        Color.secondary
    }

    static var tertiaryLabel: Color {
        Color(NSColor.tertiaryLabelColor)
    }

    static var placeholder: Color {
        Color(NSColor.placeholderTextColor)
    }

    // MARK: - Accents

    static var accent: Color {
        Color.accentColor
    }

    static var primaryButton: Color {
        Color.accentColor
    }

    static var primaryButtonHover: Color {
        Color.accentColor.opacity(0.88)
    }

    // MARK: - Status

    static var success: Color {
        Color(NSColor.systemGreen)
    }

    static var warning: Color {
        Color(NSColor.systemOrange)
    }

    static var error: Color {
        Color(NSColor.systemRed)
    }

    static var info: Color {
        Color(NSColor.systemBlue)
    }

    // MARK: - Borders / Dividers

    static var divider: Color {
        Color(NSColor.separatorColor)
    }

    static var border: Color {
        Color(NSColor.separatorColor).opacity(0.5)
    }

    static var gridLine: Color {
        Color.gray.opacity(0.22)
    }

    // MARK: - Business semantics

    /// Values that can be aggregated (e.g. numeric columns).
    static var aggregableValue: Color {
        Color.accentColor
    }

    /// Cells that have been manually corrected by the user.
    static var correctedValue: Color {
        Color(red: 0.16, green: 0.62, blue: 0.56)
    }

    static var correctedValueBackground: Color {
        Color(red: 0.16, green: 0.62, blue: 0.56).opacity(0.10)
    }

    static var selectedHeaderBackground: Color {
        Color.accentColor.opacity(0.12)
    }

    static var selectedCellBackground: Color {
        Color.accentColor.opacity(0.08)
    }

    static var dropZoneBorder: Color {
        Color.accentColor
    }

    static var dropZoneBackground: Color {
        Color.accentColor.opacity(0.06)
    }
}
