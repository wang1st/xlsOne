import SwiftUI

/// Primary call-to-action button style used for the main action in a scene
/// (e.g. "Export XLSX").
struct XPrimaryButtonStyle: ButtonStyle {
    @Environment(\.isEnabled) private var isEnabled
    @Environment(\.colorScheme) private var colorScheme

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(XFont.toolbarButton)
            .foregroundColor(.white)
            .padding(.horizontal, XSpacing.lg)
            .padding(.vertical, XSpacing.sm)
            .background(backgroundColor(isPressed: configuration.isPressed))
            .clipShape(RoundedRectangle(cornerRadius: XRadius.md, style: .continuous))
            .xShadow(isEnabled && !configuration.isPressed ? XShadow.buttonHover : ShadowStyle(color: .clear, radius: 0, x: 0, y: 0))
            .scaleEffect(configuration.isPressed ? 0.98 : 1.0)
            .animation(.easeInOut(duration: 0.1), value: configuration.isPressed)
    }

    private func backgroundColor(isPressed: Bool) -> Color {
        if !isEnabled {
            return XColor.primaryButton.opacity(0.35)
        }
        return isPressed ? XColor.primaryButtonHover : XColor.primaryButton
    }
}

/// Secondary button style for actions that are important but not primary
/// (e.g. toolbar utility buttons).
struct XSecondaryButtonStyle: ButtonStyle {
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(XFont.toolbarButton)
            .foregroundColor(isEnabled ? XColor.primaryLabel : XColor.tertiaryLabel)
            .padding(.horizontal, XSpacing.md)
            .padding(.vertical, XSpacing.sm)
            .background(backgroundColor(isPressed: configuration.isPressed))
            .clipShape(RoundedRectangle(cornerRadius: XRadius.sm, style: .continuous))
    }

    private func backgroundColor(isPressed: Bool) -> Color {
        if isPressed {
            return XColor.secondaryLabel.opacity(0.14)
        }
        return Color.clear
    }
}

/// Plain text button style used for links or low-emphasis actions.
struct XLinkButtonStyle: ButtonStyle {
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(XFont.body)
            .foregroundColor(isEnabled ? XColor.accent : XColor.tertiaryLabel)
            .opacity(configuration.isPressed ? 0.7 : 1.0)
    }
}

/// Bordered button used for destructive or configuration actions in dialogs.
struct XBorderedButtonStyle: ButtonStyle {
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(XFont.body)
            .foregroundColor(isEnabled ? XColor.primaryLabel : XColor.tertiaryLabel)
            .padding(.horizontal, XSpacing.md)
            .padding(.vertical, XSpacing.sm)
            .background(XColor.surface)
            .overlay(
                RoundedRectangle(cornerRadius: XRadius.sm, style: .continuous)
                    .stroke(XColor.border, lineWidth: 1)
            )
            .clipShape(RoundedRectangle(cornerRadius: XRadius.sm, style: .continuous))
            .opacity(configuration.isPressed ? 0.8 : 1.0)
    }
}

extension ButtonStyle where Self == XPrimaryButtonStyle {
    static var xPrimary: XPrimaryButtonStyle { XPrimaryButtonStyle() }
}

extension ButtonStyle where Self == XSecondaryButtonStyle {
    static var xSecondary: XSecondaryButtonStyle { XSecondaryButtonStyle() }
}

extension ButtonStyle where Self == XLinkButtonStyle {
    static var xLink: XLinkButtonStyle { XLinkButtonStyle() }
}

extension ButtonStyle where Self == XBorderedButtonStyle {
    static var xBordered: XBorderedButtonStyle { XBorderedButtonStyle() }
}
