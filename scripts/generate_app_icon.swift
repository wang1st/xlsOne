#!/usr/bin/env swift

import AppKit
import Foundation

// MARK: - Config

let scriptURL = URL(fileURLWithPath: CommandLine.arguments[0]).resolvingSymlinksInPath()
let repoRoot = scriptURL.deletingLastPathComponent().deletingLastPathComponent()

let assetsOutput = repoRoot
    .appendingPathComponent("App/xlsOneMacApp/Assets.xcassets/AppIcon.appiconset")

let qtResources = repoRoot
    .appendingPathComponent("cpp/app/resources")

// MARK: - Icon Sizes

let iconSizes: [(filename: String, pixels: Int)] = [
    ("icon_16x16.png",      16),
    ("icon_16x16@2x.png",   32),
    ("icon_32x32.png",      32),
    ("icon_32x32@2x.png",   64),
    ("icon_128x128.png",    128),
    ("icon_128x128@2x.png", 256),
    ("icon_256x256.png",    256),
    ("icon_256x256@2x.png", 512),
    ("icon_512x512.png",    512),
    ("icon_512x512@2x.png", 1024),
]

// sizes for .icns iconset directory
let iconsetSizes: [(name: String, pixels: Int)] = [
    ("icon_16x16.png",       16),
    ("icon_16x16@2x.png",    32),
    ("icon_32x32.png",       32),
    ("icon_32x32@2x.png",    64),
    ("icon_128x128.png",     128),
    ("icon_128x128@2x.png",  256),
    ("icon_256x256.png",     256),
    ("icon_256x256@2x.png",  512),
    ("icon_512x512.png",     512),
    ("icon_512x512@2x.png",  1024),
]

// Linux hicolor sizes
let linuxSizes: [Int] = [16, 22, 24, 32, 48, 64, 96, 128, 256, 512]

// ICO embed sizes
let icoSizes: [Int] = [16, 32, 48, 256]

// MARK: - Drawing

func drawIcon(in rect: NSRect, scale: CGFloat) {
    // Background rounded rect with blue gradient
    let bg = rect.insetBy(dx: rect.width * 0.055, dy: rect.height * 0.055)
    let bgPath = NSBezierPath(
        roundedRect: bg,
        xRadius: rect.width * 0.225,
        yRadius: rect.height * 0.225
    )
    let gradient = NSGradient(
        starting: NSColor(red: 0.10, green: 0.40, blue: 0.92, alpha: 1),
        ending: NSColor(red: 0.35, green: 0.68, blue: 0.98, alpha: 1)
    )!
    gradient.draw(in: bgPath, angle: -90)

    // White paper with shadow
    let paper = rect.insetBy(dx: rect.width * 0.195, dy: rect.height * 0.175)
    let paperPath = NSBezierPath(
        roundedRect: paper,
        xRadius: rect.width * 0.085,
        yRadius: rect.height * 0.085
    )
    NSGraphicsContext.saveGraphicsState()
    let shadow = NSShadow()
    shadow.shadowBlurRadius = 14 * scale
    shadow.shadowOffset = NSSize(width: 0, height: -7 * scale)
    shadow.shadowColor = NSColor.black.withAlphaComponent(0.18)
    shadow.set()
    NSColor.white.setFill()
    paperPath.fill()
    NSGraphicsContext.restoreGraphicsState()

    // Blue header bar on paper
    let headerH = paper.height * 0.19
    let headerRect = NSRect(x: paper.minX, y: paper.maxY - headerH,
                            width: paper.width, height: headerH)
    let headerColor = NSColor(red: 0.12, green: 0.43, blue: 0.90, alpha: 1)
    headerColor.setFill()
    let headerPath = NSBezierPath(
        roundedRect: headerRect,
        xRadius: rect.width * 0.085,
        yRadius: rect.height * 0.085
    )
    headerPath.fill()
    // Bottom of header stays flat
    NSColor(red: 0.12, green: 0.43, blue: 0.90, alpha: 1).setFill()
    NSBezierPath(rect: NSRect(
        x: headerRect.minX,
        y: headerRect.minY,
        width: headerRect.width,
        height: headerH * 0.55
    )).fill()

    // Grid
    let padX = paper.width * 0.13
    let padY = paper.height * 0.26
    let grid = paper.insetBy(dx: padX, dy: padY)
    let lineColor = NSColor(red: 0.32, green: 0.55, blue: 0.82, alpha: 0.65)
    lineColor.setStroke()

    let cols = 3, rows = 4
    let cw = grid.width / CGFloat(cols)
    let ch = grid.height / CGFloat(rows)
    let lw = max(0.8, rect.width * 0.016)

    for i in 0...cols {
        let x = grid.minX + CGFloat(i) * cw
        let p = NSBezierPath()
        p.lineWidth = lw
        p.move(to: NSPoint(x: x, y: grid.minY))
        p.line(to: NSPoint(x: x, y: grid.maxY))
        p.stroke()
    }
    for i in 0...rows {
        let y = grid.minY + CGFloat(i) * ch
        let p = NSBezierPath()
        p.lineWidth = lw
        p.move(to: NSPoint(x: grid.minX, y: y))
        p.line(to: NSPoint(x: grid.maxX, y: y))
        p.stroke()
    }

    // Highlighted cell
    let hl = NSRect(
        x: grid.minX + cw,
        y: grid.minY + ch,
        width: cw, height: ch
    ).insetBy(dx: lw * 0.6, dy: lw * 0.6)
    NSColor(red: 0.16, green: 0.55, blue: 0.94, alpha: 0.90).setFill()
    NSBezierPath(
        roundedRect: hl,
        xRadius: rect.width * 0.022,
        yRadius: rect.height * 0.022
    ).fill()

    // Orange summary bar at bottom
    let barW = grid.width * 0.70
    let barH = max(2.0, rect.height * 0.032)
    let bar = NSRect(
        x: grid.midX - barW / 2,
        y: paper.minY + paper.height * 0.10,
        width: barW, height: barH
    )
    NSColor(red: 0.96, green: 0.55, blue: 0.12, alpha: 0.92).setFill()
    NSBezierPath(
        roundedRect: bar,
        xRadius: barH / 2,
        yRadius: barH / 2
    ).fill()
}

// MARK: - PNG generation

func renderPNG(pixels: Int) -> Data {
    let bitmap = NSBitmapImageRep(
        bitmapDataPlanes: nil,
        pixelsWide: pixels, pixelsHigh: pixels,
        bitsPerSample: 8, samplesPerPixel: 4,
        hasAlpha: true, isPlanar: false,
        colorSpaceName: .deviceRGB,
        bytesPerRow: 0, bitsPerPixel: 0
    )!
    bitmap.size = NSSize(width: pixels, height: pixels)

    let ctx = NSGraphicsContext(bitmapImageRep: bitmap)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = ctx
    drawIcon(
        in: NSRect(x: 0, y: 0, width: pixels, height: pixels),
        scale: CGFloat(pixels) / 1024.0
    )
    NSGraphicsContext.restoreGraphicsState()

    return bitmap.representation(using: .png, properties: [:])!
}

// MARK: - ICO generation

func writeICO(_ pngDataArray: [(size: Int, data: Data)], to url: URL) {
    // ICO format: header (6 bytes) + icon dir entries (16 bytes each) + image data
    let count = pngDataArray.count
    var output = Data()

    // Header
    output.append(contentsOf: [0, 0])             // reserved
    output.append(contentsOf: [1, 0])             // type: ICO
    output.append(contentsOf: withUnsafeBytes(of: UInt16(count).littleEndian) { Data($0) })

    // Calculate offset of first image data
    let headerSize = 6
    let dirSize = count * 16
    var imageOffset = headerSize + dirSize

    var dirEntries = Data()
    var imageData = Data()

    for (size, pngData) in pngDataArray {
        let s = UInt8(min(size, 256) % 256) // 256 → 0 in ICO
        dirEntries.append(contentsOf: [s, s, 0, 0, 1, 0, 0, 0]) // w, h, palette, reserved, color planes, bpp (placeholder)

        let dataSize = UInt32(pngData.count)
        dirEntries.append(contentsOf: withUnsafeBytes(of: dataSize.littleEndian) { Data($0) })
        dirEntries.append(contentsOf: withUnsafeBytes(of: UInt32(imageOffset).littleEndian) { Data($0) })

        imageData.append(pngData)
        imageOffset += pngData.count
    }

    output.append(dirEntries)
    output.append(imageData)

    try! output.write(to: url)
}

// MARK: - Main execution

let fm = FileManager.default

// Ensure output directories exist
try? fm.createDirectory(at: assetsOutput, withIntermediateDirectories: true)
try? fm.createDirectory(at: qtResources, withIntermediateDirectories: true)

// Step 1: Generate PNGs for macOS Assets.xcassets
print("=== Generating PNG icons for macOS ===")
for slot in iconSizes {
    let data = renderPNG(pixels: slot.pixels)
    let url = assetsOutput.appendingPathComponent(slot.filename)
    try data.write(to: url)
    print("  \(url.path)")
}

// Step 2: Generate .icns via iconutil
print("\n=== Generating .icns for macOS Qt ===")
let iconsetDir = qtResources.appendingPathComponent("xlsOne.iconset")
try? fm.removeItem(at: iconsetDir)
try fm.createDirectory(at: iconsetDir, withIntermediateDirectories: true)

for entry in iconsetSizes {
    let data = renderPNG(pixels: entry.pixels)
    let url = iconsetDir.appendingPathComponent(entry.name)
    try data.write(to: url)
    print("  \(url.lastPathComponent)")
}

let icnsOutput = qtResources.appendingPathComponent("xlsOne.icns")
try? fm.removeItem(at: icnsOutput)

let task = Process()
task.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
task.arguments = ["-c", "icns", "-o", icnsOutput.path, iconsetDir.path]
try task.run()
task.waitUntilExit()

if task.terminationStatus == 0 {
    try? fm.removeItem(at: iconsetDir) // cleanup
    print("  -> \(icnsOutput.path)")
} else {
    // Fallback: copy largest PNG as .icns placeholder
    let png256 = renderPNG(pixels: 256)
    try png256.write(to: icnsOutput)
    print("  iconutil failed, using PNG fallback")
}
try? fm.removeItem(at: iconsetDir)

// Step 3: Generate .ico for Windows
print("\n=== Generating .ico for Windows ===")
var icoEntries: [(size: Int, data: Data)] = []
for size in icoSizes {
    icoEntries.append((size, renderPNG(pixels: size)))
}
let icoOutput = qtResources.appendingPathComponent("xlsOne.ico")
writeICO(icoEntries, to: icoOutput)
print("  \(icoOutput.path)")

// Step 4: Generate multi-size PNGs for Linux hicolor
print("\n=== Generating Linux hicolor PNGs ===")
for size in linuxSizes {
    let data = renderPNG(pixels: size)
    let url = qtResources.appendingPathComponent("xlsOne_\(size)x\(size).png")
    try data.write(to: url)
}
// Also create a generic name
let mainPNG = renderPNG(pixels: 256)
try mainPNG.write(to: qtResources.appendingPathComponent("xlsOne.png"))
print("  \(linuxSizes.count) sizes generated in \(qtResources.path)")

// Step 5: Update Assets.xcassets Contents.json
let contentsJSON = """
{
  "images" : [
    {"idiom" : "mac", "scale" : "1x", "size" : "16x16", "filename" : "icon_16x16.png"},
    {"idiom" : "mac", "scale" : "2x", "size" : "16x16", "filename" : "icon_16x16@2x.png"},
    {"idiom" : "mac", "scale" : "1x", "size" : "32x32", "filename" : "icon_32x32.png"},
    {"idiom" : "mac", "scale" : "2x", "size" : "32x32", "filename" : "icon_32x32@2x.png"},
    {"idiom" : "mac", "scale" : "1x", "size" : "128x128", "filename" : "icon_128x128.png"},
    {"idiom" : "mac", "scale" : "2x", "size" : "128x128", "filename" : "icon_128x128@2x.png"},
    {"idiom" : "mac", "scale" : "1x", "size" : "256x256", "filename" : "icon_256x256.png"},
    {"idiom" : "mac", "scale" : "2x", "size" : "256x256", "filename" : "icon_256x256@2x.png"},
    {"idiom" : "mac", "scale" : "1x", "size" : "512x512", "filename" : "icon_512x512.png"},
    {"idiom" : "mac", "scale" : "2x", "size" : "512x512", "filename" : "icon_512x512@2x.png"}
  ],
  "info" : {"author" : "xcode", "version" : 1}
}
"""
try contentsJSON.write(to: assetsOutput.appendingPathComponent("Contents.json"),
                       atomically: true, encoding: .utf8)

print("\n✅ All icons generated successfully!")
