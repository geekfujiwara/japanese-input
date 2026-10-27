// Draws the macOS installer's background (tools/macos-pkg-resources/background*.png): the app icon and name in the
// lower left, under the installer's list of steps, on a transparent canvas.
//
//   swift tools/make-installer-background.swift <AppIcon.icns> <output folder>
import AppKit

let arguments = CommandLine.arguments
guard arguments.count == 3, let icon = NSImage(contentsOfFile: arguments[1]) else {
    print("usage: make-installer-background.swift <AppIcon.icns> <output folder>")
    exit(1)
}
let folder = URL(fileURLWithPath: arguments[2], isDirectory: true)
let size = NSSize(width: 620, height: 418) // the installer window's content, in points
let scale: CGFloat = 2

func draw(dark: Bool, to url: URL) {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(size.width * scale), pixelsHigh: Int(size.height * scale),
                               bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                               colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    rep.size = size // 144 dpi: drawn at 620 x 418 points
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)

    // A soft green glow behind the icon.
    let accent = NSColor(red: 0.31, green: 0.62, blue: 0.43, alpha: 1)
    let glow = NSGradient(colors: [accent.withAlphaComponent(dark ? 0.35 : 0.22), accent.withAlphaComponent(0)])!
    glow.draw(in: NSRect(x: -60, y: -60, width: 300, height: 300), relativeCenterPosition: .zero)

    icon.draw(in: NSRect(x: 34, y: 58, width: 96, height: 96))
    let name = NSAttributedString(string: "Astelio", attributes: [
        .font: NSFont.systemFont(ofSize: 22, weight: .semibold),
        .foregroundColor: dark ? NSColor.white : NSColor(red: 0.125, green: 0.145, blue: 0.129, alpha: 1),
    ])
    name.draw(at: NSPoint(x: 42, y: 26))
    let tagline = NSAttributedString(string: "日本語入力", attributes: [
        .font: NSFont.systemFont(ofSize: 11),
        .foregroundColor: dark ? NSColor(white: 0.7, alpha: 1) : NSColor(white: 0.4, alpha: 1),
    ])
    tagline.draw(at: NSPoint(x: 44, y: 12))

    NSGraphicsContext.restoreGraphicsState()
    try! rep.representation(using: .png, properties: [:])!.write(to: url)
    print("wrote \(url.path)")
}

draw(dark: false, to: folder.appendingPathComponent("background.png"))
draw(dark: true, to: folder.appendingPathComponent("background-dark.png"))
