import SwiftUI

/// Central theme access point for xlsOne.
///
/// Use `XTheme.color`, `XTheme.font`, `XTheme.spacing`, etc. in views, or
/// import the individual token files directly. Keeping all tokens in one
/// namespace makes it easy to discover the design system.
enum XTheme {
    typealias Color = XColor
    typealias Font = XFont
    typealias Spacing = XSpacing
    typealias Radius = XRadius
    typealias Shadow = XShadow
}
