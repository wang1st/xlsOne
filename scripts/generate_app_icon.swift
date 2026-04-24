#!/usr/bin/env swift

import AppKit
import Foundation

struct IconSlot {
    let filename: String
    let pixels: Int
}

let scriptURL = URL(fileURLWithPath: CommandLine.arguments[0]).resolvingSymlinksInPath()
let repoRoot = scriptURL.deletingLastPathComponent().deletingLastPathComponent()
let outputDirectory: URL

if CommandLine.arguments.count > 1 {
    outputDirectory = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
} else {
    outputDirectory = repoRoot
        .appendingPathComponent("App", isDirectory: true)
        .appendingPathComponent("xlsOneMacApp", isDirectory: true)
        .appendingPathComponent("Assets.xcassets", isDirectory: true)
        .appendingPathComponent("AppIcon.appiconset", isDirectory: true)
}

let slots: [IconSlot] = [
    .init(filename: "icon_16x16.png", pixels: 16),
    .init(filename: "icon_16x16@2x.png", pixels: 32),
    .init(filename: "icon_32x32.png", pixels: 32),
    .init(filename: "icon_32x32@2x.png", pixels: 64),
    .init(filename: "icon_128x128.png", pixels: 128),
    .init(filename: "icon_128x128@2x.png", pixels: 256),
    .init(filename: "icon_256x256.png", pixels: 256),
    .init(filename: "icon_256x256@2x.png", pixels: 512),
    .init(filename: "icon_512x512.png", pixels: 512),
    .init(filename: "icon_512x512@2x.png", pixels: 1024)
]

func makeBitmap(size: Int) -> NSBitmapImageRep {
    let rep = NSBitmapImageRep(
        bitmapDataPlanes: nil,
        pixelsWide: size,
        pixelsHigh: size,
        bitsPerSample: 8,
        samplesPerPixel: 4,
        hasAlpha: true,
        isPlanar: false,
        colorSpaceName: .deviceRGB,
        bytesPerRow: 0,
        bitsPerPixel: 0
    )!
    rep.size = NSSize(width: size, height: size)
    return rep
}

func drawIcon(in rect: NSRect, scale: CGFloat) {
    let background = NSBezierPath(
        roundedRect: rect.insetBy(dx: rect.width * 0.06, dy: rect.height * 0.06),
        xRadius: rect.width * 0.22,
        yRadius: rect.height * 0.22
    )
    let gradient = NSGradient(
        starting: NSColor(calibratedRed: 0.14, green: 0.45, blue: 0.93, alpha: 1),
        ending: NSColor(calibratedRed: 0.47, green: 0.76, blue: 0.99, alpha: 1)
    )!
    gradient.draw(in: background, angle: -90)

    let paperRect = rect.insetBy(dx: rect.width * 0.20, dy: rect.height * 0.18)
    let paperPath = NSBezierPath(
        roundedRect: paperRect,
        xRadius: rect.width * 0.08,
        yRadius: rect.height * 0.08
    )
    NSGraphicsContext.saveGraphicsState()
    let shadow = NSShadow()
    shadow.shadowBlurRadius = 16 * scale
    shadow.shadowOffset = NSSize(width: 0, height: -8 * scale)
    shadow.shadowColor = NSColor.black.withAlphaComponent(0.16)
    shadow.set()
    NSColor.white.setFill()
    paperPath.fill()
    NSGraphicsContext.restoreGraphicsState()

    let accentRect = NSRect(
        x: paperRect.minX,
        y: paperRect.maxY - paperRect.height * 0.20,
        width: paperRect.width,
        height: paperRect.height * 0.20
    )
    NSColor(calibratedRed: 0.15, green: 0.45, blue: 0.92, alpha: 1).setFill()
    NSBezierPath(
        roundedRect: accentRect,
        xRadius: rect.width * 0.08,
        yRadius: rect.height * 0.08
    ).fill()
    NSColor.white.withAlphaComponent(0.92).setFill()
    NSBezierPath(rect: NSRect(
        x: accentRect.minX,
        y: accentRect.minY,
        width: accentRect.width,
        height: accentRect.height * 0.62
    )).fill()

    let gridInsetX = paperRect.width * 0.14
    let gridInsetY = paperRect.height * 0.26
    let gridRect = paperRect.insetBy(dx: gridInsetX, dy: gridInsetY)
    let strokeColor = NSColor(calibratedRed: 0.34, green: 0.52, blue: 0.80, alpha: 1)
    strokeColor.setStroke()

    let columns = 3
    let rows = 4
    let cellWidth = gridRect.width / CGFloat(columns)
    let cellHeight = gridRect.height / CGFloat(rows)
    let lineWidth = max(1.0, rect.width * 0.018)

    for index in 0...columns {
        let x = gridRect.minX + CGFloat(index) * cellWidth
        let path = NSBezierPath()
        path.lineWidth = lineWidth
        path.move(to: NSPoint(x: x, y: gridRect.minY))
        path.line(to: NSPoint(x: x, y: gridRect.maxY))
        path.stroke()
    }

    for index in 0...rows {
        let y = gridRect.minY + CGFloat(index) * cellHeight
        let path = NSBezierPath()
        path.lineWidth = lineWidth
        path.move(to: NSPoint(x: gridRect.minX, y: y))
        path.line(to: NSPoint(x: gridRect.maxX, y: y))
        path.stroke()
    }

    let highlightRect = NSRect(
        x: gridRect.minX + cellWidth,
        y: gridRect.minY + cellHeight,
        width: cellWidth,
        height: cellHeight
    ).insetBy(dx: lineWidth * 0.7, dy: lineWidth * 0.7)
    NSColor(calibratedRed: 0.18, green: 0.58, blue: 0.95, alpha: 0.92).setFill()
    NSBezierPath(
        roundedRect: highlightRect,
        xRadius: rect.width * 0.02,
        yRadius: rect.height * 0.02
    ).fill()

    let summaryBarRect = NSRect(
        x: gridRect.minX,
        y: paperRect.minY + paperRect.height * 0.10,
        width: gridRect.width * 0.68,
        height: max(2.0, rect.height * 0.03)
    )
    NSColor(calibratedRed: 0.95, green: 0.59, blue: 0.18, alpha: 0.95).setFill()
    NSBezierPath(
        roundedRect: summaryBarRect,
        xRadius: summaryBarRect.height / 2,
        yRadius: summaryBarRect.height / 2
    ).fill()
}

try FileManager.default.createDirectory(at: outputDirectory, withIntermediateDirectories: true)

for slot in slots {
    let bitmap = makeBitmap(size: slot.pixels)
    let context = NSGraphicsContext(bitmapImageRep: bitmap)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = context
    drawIcon(
        in: NSRect(x: 0, y: 0, width: slot.pixels, height: slot.pixels),
        scale: CGFloat(slot.pixels) / 1024.0
    )
    NSGraphicsContext.restoreGraphicsState()

    let data = bitmap.representation(using: .png, properties: [:])!
    let fileURL = outputDirectory.appendingPathComponent(slot.filename)
    try data.write(to: fileURL)
    print("Generated \(fileURL.path)")
}
