import SwiftUI

/// Shadow tokens for xlsOne.
enum XShadow {
    static let card: ShadowStyle = ShadowStyle(
        color: Color.black.opacity(0.04),
        radius: 16,
        x: 0,
        y: 8
    )

    static let dropZone: ShadowStyle = ShadowStyle(
        color: Color.black.opacity(0.08),
        radius: 24,
        x: 0,
        y: 12
    )

    static let buttonHover: ShadowStyle = ShadowStyle(
        color: Color.black.opacity(0.06),
        radius: 6,
        x: 0,
        y: 2
    )
}

struct ShadowStyle {
    let color: Color
    let radius: CGFloat
    let x: CGFloat
    let y: CGFloat

    var swiftUIShadow: some ViewModifier {
        ShadowModifier(style: self)
    }
}

private struct ShadowModifier: ViewModifier {
    let style: ShadowStyle

    func body(content: Content) -> some View {
        content
            .shadow(color: style.color, radius: style.radius, x: style.x, y: style.y)
    }
}

extension View {
    func xShadow(_ style: ShadowStyle) -> some View {
        modifier(style.swiftUIShadow)
    }
}
