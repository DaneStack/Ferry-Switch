import AppKit

// Store banner, 848x208 — the size hb-app.store crops to. Drawn rather
// than cropped from a screenshot so the app's identity (palette, ferry
// glyph, the layered water motif from drawBackgroundWaves) reads at
// banner scale instead of becoming an illegible thin strip.

let W: CGFloat = 848
let H: CGFloat = 208

// Palette lifted from theme.cpp's dark mode.
let bg        = NSColor(srgbRed:  11/255, green:  15/255, blue:  20/255, alpha: 1)
let deep      = NSColor(srgbRed:   9/255, green:  26/255, blue:  30/255, alpha: 1)
let accent    = NSColor(srgbRed:  47/255, green: 217/255, blue: 196/255, alpha: 1)
let textPri   = NSColor(srgbRed: 234/255, green: 242/255, blue: 242/255, alpha: 1)
let textSec   = NSColor(srgbRed: 143/255, green: 163/255, blue: 168/255, alpha: 1)

let rep = NSBitmapImageRep(
    bitmapDataPlanes: nil, pixelsWide: Int(W), pixelsHigh: Int(H),
    bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
    colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!

NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
let ctx = NSGraphicsContext.current!.cgContext

// --- background: flat base, then a depth wash rising from the bottom ---
bg.setFill()
NSRect(x: 0, y: 0, width: W, height: H).fill()

let grad = NSGradient(colors: [deep.withAlphaComponent(0.95), bg.withAlphaComponent(0.0)])!
grad.draw(in: NSRect(x: 0, y: 0, width: W, height: H * 0.75), angle: 90)

// --- water: three drifting bands, same harmonic shape as the app's ---
struct Layer { let baseY: CGFloat; let amp: CGFloat; let wave: CGFloat; let phase: CGFloat; let fill: CGFloat; let crest: CGFloat }
let layers = [
    Layer(baseY: 74, amp: 9,  wave: 520, phase: 0.0, fill: 0.07, crest: 0.20),
    Layer(baseY: 52, amp: 12, wave: 360, phase: 2.1, fill: 0.10, crest: 0.26),
    Layer(baseY: 30, amp: 8,  wave: 250, phase: 4.4, fill: 0.14, crest: 0.34),
]
let tau = CGFloat.pi * 2

for layer in layers {
    let path = NSBezierPath()
    path.move(to: NSPoint(x: 0, y: 0))
    var x: CGFloat = 0
    while x <= W {
        let primary = sin(tau * x / layer.wave + layer.phase)
        let harmonic = sin(tau * x / (layer.wave * 0.43) + layer.phase * 1.7)
        let y = layer.baseY + (primary + 0.32 * harmonic) * layer.amp
        if x == 0 { path.line(to: NSPoint(x: 0, y: y)) }
        path.line(to: NSPoint(x: x, y: y))
        x += 8
    }
    path.line(to: NSPoint(x: W, y: 0))
    path.close()
    accent.withAlphaComponent(layer.fill).setFill()
    path.fill()

    // Crest, drawn separately so the fill's edge doesn't soften it.
    let crest = NSBezierPath()
    crest.lineWidth = 1.4
    var cx: CGFloat = 0
    while cx <= W {
        let primary = sin(tau * cx / layer.wave + layer.phase)
        let harmonic = sin(tau * cx / (layer.wave * 0.43) + layer.phase * 1.7)
        let y = layer.baseY + (primary + 0.32 * harmonic) * layer.amp
        if cx == 0 { crest.move(to: NSPoint(x: cx, y: y)) } else { crest.line(to: NSPoint(x: cx, y: y)) }
        cx += 8
    }
    accent.withAlphaComponent(layer.crest).setStroke()
    crest.stroke()
}

// --- motes ---
for i in 0..<18 {
    let mx = CGFloat((i * 197 + 40) % Int(W))
    let my = CGFloat(18 + (i * 53) % 96)
    let r = CGFloat(1.5 + CGFloat(i % 3) * 0.9)
    accent.withAlphaComponent(0.16).setFill()
    NSBezierPath(ovalIn: NSRect(x: mx - r, y: my - r, width: r * 2, height: r * 2)).fill()
}

// --- ferry glyph: trapezoid hull with a cabin block, same as ui.cpp ---
func drawBoat(cx: CGFloat, cy: CGFloat, s: CGFloat, color: NSColor) {
    color.setFill()
    let hull = NSBezierPath()
    hull.move(to: NSPoint(x: cx - 15 * s, y: cy - 3 * s))
    hull.line(to: NSPoint(x: cx + 15 * s, y: cy - 3 * s))
    hull.line(to: NSPoint(x: cx + 11 * s, y: cy - 12 * s))
    hull.line(to: NSPoint(x: cx - 11 * s, y: cy - 12 * s))
    hull.close()
    hull.fill()

    let cabin = NSBezierPath(
        roundedRect: NSRect(x: cx - 7 * s, y: cy - 4 * s, width: 14 * s, height: 12 * s),
        xRadius: 3 * s, yRadius: 3 * s)
    cabin.fill()
}

let glyphCX: CGFloat = 104
let glyphCY: CGFloat = 126
drawBoat(cx: glyphCX, cy: glyphCY, s: 2.5, color: accent)

// --- wordmark + tagline ---
func draw(_ text: String, x: CGFloat, y: CGFloat, size: CGFloat, weight: NSFont.Weight, color: NSColor, tracking: CGFloat = 0) {
    let attrs: [NSAttributedString.Key: Any] = [
        .font: NSFont.systemFont(ofSize: size, weight: weight),
        .foregroundColor: color,
        .kern: tracking,
    ]
    NSAttributedString(string: text, attributes: attrs).draw(at: NSPoint(x: x, y: y))
}

draw("Ferry", x: 168, y: 104, size: 62, weight: .bold, color: textPri, tracking: -1.5)
draw("FTP server for Nintendo Switch", x: 172, y: 74, size: 19, weight: .regular, color: textSec, tracking: 0.2)

// --- capability chips, right-aligned ---
let chips = ["SD Card", "NAND", "Installed Games", "Saves"]
let chipFont = NSFont.systemFont(ofSize: 14, weight: .medium)
var widths: [CGFloat] = []
for c in chips {
    widths.append(NSAttributedString(string: c, attributes: [.font: chipFont]).size().width + 26)
}
let gap: CGFloat = 9
let totalW = widths.reduce(0, +) + gap * CGFloat(chips.count - 1)
var chipX = W - 56 - totalW
// Centred on the wordmark+tagline block (which spans ~74-178), not on
// the banner, so the right side reads as the same row as the title.
let chipY: CGFloat = 111
for (i, c) in chips.enumerated() {
    let rect = NSRect(x: chipX, y: chipY, width: widths[i], height: 30)
    let bgPath = NSBezierPath(roundedRect: rect, xRadius: 15, yRadius: 15)
    accent.withAlphaComponent(0.10).setFill()
    bgPath.fill()
    accent.withAlphaComponent(0.30).setStroke()
    bgPath.lineWidth = 1
    bgPath.stroke()

    let attrs: [NSAttributedString.Key: Any] = [.font: chipFont, .foregroundColor: accent]
    let s = NSAttributedString(string: c, attributes: attrs)
    let sz = s.size()
    s.draw(at: NSPoint(x: rect.midX - sz.width / 2, y: rect.midY - sz.height / 2))
    chipX += widths[i] + gap
}

NSGraphicsContext.restoreGraphicsState()

let out = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "banner.png"
guard let data = rep.representation(using: .png, properties: [:]) else {
    fatalError("png encode failed")
}
try! data.write(to: URL(fileURLWithPath: out))
print("wrote \(out) — \(Int(W))x\(Int(H))")
