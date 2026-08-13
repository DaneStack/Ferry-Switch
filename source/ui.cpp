#include "ui.hpp"

#include <SDL2/SDL2_gfxPrimitives.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "file_icons.hpp"
#include "theme.hpp"
#include "vendor/qrcodegen.h"

namespace ui {

namespace {

Uint32 packColor(SDL_Color c) {
    return (static_cast<Uint32>(c.r)) |
           (static_cast<Uint32>(c.g) << 8) |
           (static_cast<Uint32>(c.b) << 16) |
           (static_cast<Uint32>(c.a) << 24);
}

constexpr SDL_Color connectedColor { 52, 211, 153, 255 };  // green
constexpr SDL_Color waitingColor { 255, 107, 91, 255 };    // red (== Theme::danger)
constexpr SDL_Color receivedColor { 52, 211, 153, 255 };   // green — a file landed on the console (STOR)
constexpr SDL_Color sentColor { 250, 204, 21, 255 };       // yellow — the console sent a file out (RETR)

// Shared between drawServerFrame's rendering and rootCardAt()'s touch
// hit-testing, same reasoning as HeaderTabs/computeHeaderTabs below.
constexpr int serverLeft = 40;
constexpr int serverRightPanelX = 680;

size_t g_browseScrollOffset = 0;
bool g_browseDragging = false;

long long nowMs() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

std::string formatClockTime(long long epochMs) {
    time_t t = static_cast<time_t>(epochMs / 1000);
    struct tm* tmv = std::localtime(&t);
    if (!tmv) {
        return "";
    }
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M", tmv);
    return buf;
}

int textWidth(TTF_Font* font, const std::string& text) {
    if (!font || text.empty()) {
        return 0;
    }
    int w = 0;
    int h = 0;
    TTF_SizeUTF8(font, text.c_str(), &w, &h);
    return w;
}

// Shrinks `text` (from the end, never splitting a multi-byte UTF-8
// sequence) until it plus "..." fits within maxWidth — used for log rows,
// where a long filename would otherwise run into the size/time columns
// or, when the QR panel is showing, right off the edge of its column.
std::string truncateToWidth(TTF_Font* font, const std::string& text, int maxWidth) {
    if (maxWidth <= 0) {
        return "";
    }
    if (textWidth(font, text) <= maxWidth) {
        return text;
    }
    const std::string ellipsis = "...";
    if (textWidth(font, ellipsis) >= maxWidth) {
        return ellipsis;
    }
    std::string result = text;
    while (!result.empty()) {
        result.pop_back();
        while (!result.empty() && (static_cast<unsigned char>(result.back()) & 0xC0) == 0x80) {
            result.pop_back();
        }
        if (textWidth(font, result + ellipsis) <= maxWidth) {
            return result + ellipsis;
        }
    }
    return ellipsis;
}

void drawTextRight(SDL_Renderer* renderer, TTF_Font* font, const std::string& text,
                    int rightX, int y, SDL_Color color) {
    drawText(renderer, font, text, rightX - textWidth(font, text), y, color);
}

void drawArrow(SDL_Renderer* renderer, int cx, int cy, int size, bool pointingDown, SDL_Color color) {
    Uint32 c = packColor(color);
    if (pointingDown) {
        filledTrigonColor(renderer, cx - size, cy - size, cx + size, cy - size, cx, cy + size, c);
    } else {
        filledTrigonColor(renderer, cx - size, cy + size, cx + size, cy + size, cx, cy - size, c);
    }
}

void drawX(SDL_Renderer* renderer, int cx, int cy, int size, SDL_Color color) {
    Uint32 c = packColor(color);
    thickLineColor(renderer, cx - size, cy - size, cx + size, cy + size, 3, c);
    thickLineColor(renderer, cx - size, cy + size, cx + size, cy - size, 3, c);
}

// A tiny version of the app's own ferry-boat mark, echoed in the header
// next to the wordmark — hull as a trapezoid, small cabin block on top.
void drawBoatGlyph(SDL_Renderer* renderer, int cx, int cy, SDL_Color color) {
    Sint16 hullX[4] = {
        static_cast<Sint16>(cx - 15), static_cast<Sint16>(cx + 15),
        static_cast<Sint16>(cx + 11), static_cast<Sint16>(cx - 11),
    };
    Sint16 hullY[4] = {
        static_cast<Sint16>(cy + 3), static_cast<Sint16>(cy + 3),
        static_cast<Sint16>(cy + 12), static_cast<Sint16>(cy + 12),
    };
    filledPolygonColor(renderer, hullX, hullY, 4, packColor(color));
    SDL_Rect cabin { cx - 7, cy - 8, 14, 12 };
    fillRoundedRect(renderer, cabin, 3, color);
}

struct HeaderTabs {
    SDL_Rect server;
    SDL_Rect browse;
};

HeaderTabs computeHeaderTabs(TTF_Font* fontBold) {
    const int tabY = 26;
    const int tabH = 48;
    int tabX = 210;

    HeaderTabs tabs;
    int serverW = textWidth(fontBold, "Server") + 48;
    tabs.server = { tabX, tabY, serverW, tabH };
    tabX += serverW + 14;

    int browseW = textWidth(fontBold, "Browse") + 48;
    tabs.browse = { tabX, tabY, browseW, tabH };
    return tabs;
}

bool pointInRect(int x, int y, SDL_Rect r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

struct RootCards {
    SDL_Rect sd;
    SDL_Rect nand;
    SDL_Rect games;
    SDL_Rect saves;
};

// Only ever shown/tappable while disconnected (switching root with a
// client attached isn't allowed — see FtpServer::setRoot) — fixed
// position, so rendering and touch hit-testing share this one source.
// 2x2 grid — four options no longer fit comfortably in a single row at a
// tappable size. Card height has to clear the *bold* font's actual size
// (30px, see main.cpp) between the title and subtitle baselines, not just
// look like enough room in the abstract — too tight here previously made
// the two lines run into each other.
//
// Lives inside the QR panel (right side), below the address/subtitle
// block, not the left column — the left column only has room for
// status/hint text above the activity log, and cramming a 2x2 grid in
// there either overlapped that text or pushed the log off-screen. The QR
// panel had slack below the address block anyway.
RootCards computeRootCards() {
    int panelLeft = serverRightPanelX + 20;
    int panelRight = screenWidth - serverLeft - 20;
    int y = 510;
    int h = 76;
    int rowGap = 10;
    int colGap = 12;
    int totalW = panelRight - panelLeft;
    int w = (totalW - colGap) / 2;
    int row2Y = y + h + rowGap;
    return RootCards{
        SDL_Rect{ panelLeft, y, w, h },
        SDL_Rect{ panelLeft + w + colGap, y, w, h },
        SDL_Rect{ panelLeft, row2Y, w, h },
        SDL_Rect{ panelLeft + w + colGap, row2Y, w, h },
    };
}

void drawRootCard(SDL_Renderer* renderer, TTF_Font* fontBold, TTF_Font* fontRegular, SDL_Rect rect,
                   const std::string& title, const std::string& subtitle, bool active, bool focused) {
    fillRoundedRect(renderer, rect, 14, active ? Theme::accentDim : Theme::panel);
    if (active) {
        roundedRectangleColor(renderer, rect.x, rect.y, rect.x + rect.w, rect.y + rect.h, 14, packColor(Theme::accent));
    } else if (focused) {
        roundedRectangleColor(renderer, rect.x, rect.y, rect.x + rect.w, rect.y + rect.h, 14,
                              packColor(Theme::textSecondary));
    }
    drawText(renderer, fontBold, title, rect.x + 18, rect.y + 14, active ? Theme::accent : Theme::textPrimary);
    drawText(renderer, fontRegular, subtitle, rect.x + 18, rect.y + 46, Theme::textTertiary);
}

void drawProgressBar(SDL_Renderer* renderer, SDL_Rect rect, double fraction, SDL_Color fillColor) {
    fillRoundedRect(renderer, rect, rect.h / 2, Theme::background);
    if (fraction > 0.0) {
        int w = static_cast<int>(rect.w * (fraction > 1.0 ? 1.0 : fraction));
        if (w < rect.h) {
            w = rect.h;
        }
        SDL_Rect fillRect { rect.x, rect.y, w, rect.h };
        fillRoundedRect(renderer, fillRect, rect.h / 2, fillColor);
    }
}

// A couple of faint wave lines echoing the boat/water motif from the app
// icon, low enough in contrast that they read as texture rather than
// content — drawn first, so everything else layers cleanly on top.
void drawBackgroundWaves(SDL_Renderer* renderer) {
    SDL_Color waveColor = Theme::accent;
    Uint8 baseAlpha = 14;
    double t = static_cast<double>(nowMs() % 20000) / 20000.0;
    double drift = t * 40.0;

    struct Wave { double baseY; double amplitude; double wavelength; Uint8 alpha; };
    const Wave waves[] = {
        { 560.0, 18.0, 260.0, baseAlpha },
        { 620.0, 24.0, 340.0, static_cast<Uint8>(baseAlpha + 6) },
        { 675.0, 14.0, 220.0, static_cast<Uint8>(baseAlpha + 4) },
    };

    for (const auto& wave : waves) {
        SDL_Color c = waveColor;
        c.a = wave.alpha;
        Uint32 packed = packColor(c);
        int prevX = -40;
        int prevY = static_cast<int>(wave.baseY + std::sin((prevX + drift) / wave.wavelength) * wave.amplitude);
        for (int x = -40; x <= screenWidth + 40; x += 24) {
            int py = static_cast<int>(wave.baseY + std::sin((x + drift) / wave.wavelength) * wave.amplitude);
            aalineColor(renderer, prevX, prevY, x, py, packed);
            prevX = x;
            prevY = py;
        }
    }
}

// STOR doesn't declare a size upfront, so there's no real fraction to show
// while receiving — an empty bar reads as broken/stuck, so a segment slides
// back and forth instead to signal "still working" without claiming a
// percentage that isn't known.
void drawIndeterminateBar(SDL_Renderer* renderer, SDL_Rect rect, SDL_Color color) {
    fillRoundedRect(renderer, rect, rect.h / 2, Theme::background);
    double period = 1400.0;
    double t = static_cast<double>(nowMs() % static_cast<long long>(period)) / period;
    double phase = t < 0.5 ? t * 2.0 : (1.0 - t) * 2.0;
    int segW = rect.w / 3;
    int segX = rect.x + static_cast<int>(phase * (rect.w - segW));
    SDL_Rect seg { segX, rect.y, segW, rect.h };
    fillRoundedRect(renderer, seg, rect.h / 2, color);
}

// QR codes need a light background with dark modules to scan reliably
// regardless of app theme, plus the standard 4-module quiet zone — so this
// draws its own white card rather than trying to fit the dark theme.
void drawQrCode(SDL_Renderer* renderer, int x, int y, int boxSize, const std::string& text) {
    uint8_t tempBuffer[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    bool ok = qrcodegen_encodeText(text.c_str(), tempBuffer, qrcode, qrcodegen_Ecc_LOW,
                                    qrcodegen_VERSION_MIN, 10, qrcodegen_Mask_AUTO, true);

    SDL_Rect card { x, y, boxSize, boxSize };
    fillRoundedRect(renderer, card, 12, SDL_Color{ 255, 255, 255, 255 });
    // A thin border keeps the (always-white) card visible against its
    // panel in light theme, where the two are otherwise close in tone.
    roundedRectangleColor(renderer, card.x, card.y, card.x + card.w, card.y + card.h, 12, packColor(Theme::divider));
    if (!ok) {
        return;
    }

    int size = qrcodegen_getSize(qrcode);
    int quiet = 4;
    double moduleSize = static_cast<double>(boxSize) / (size + quiet * 2);
    for (int qy = 0; qy < size; ++qy) {
        for (int qx = 0; qx < size; ++qx) {
            if (!qrcodegen_getModule(qrcode, qx, qy)) {
                continue;
            }
            SDL_Rect module {
                x + static_cast<int>((qx + quiet) * moduleSize),
                y + static_cast<int>((qy + quiet) * moduleSize),
                static_cast<int>(moduleSize) + 1,
                static_cast<int>(moduleSize) + 1,
            };
            SDL_SetRenderDrawColor(renderer, 10, 10, 10, 255);
            SDL_RenderFillRect(renderer, &module);
        }
    }
}

// A small "key cap" badge so button names read as distinct, tappable-
// looking tokens instead of blending into the description text next to
// them — returns the x position right after it, for chaining.
int drawKeyBadge(SDL_Renderer* renderer, TTF_Font* font, int x, int y, const std::string& key) {
    int w = textWidth(font, key);
    SDL_Rect badge { x, y, w + 20, 28 };
    fillRoundedRect(renderer, badge, 8, Theme::accentDim);
    drawText(renderer, font, key, x + 10, y + 3, Theme::accent);
    return x + badge.w + 12;
}

void drawControlRow(SDL_Renderer* renderer, TTF_Font* font, int x, int y, const std::string& key,
                     const std::string& desc) {
    int textX = drawKeyBadge(renderer, font, x, y, key);
    drawText(renderer, font, desc, textX, y + 3, Theme::textSecondary);
}

// Every entry — transfer or not — renders as exactly one line, all in the
// same font/size, so the log doesn't read as a jumble of differently
// weighted rows. Transfer rows get an icon, a (decorative, always-full)
// progress bar, a size, and a time; everything else just gets an icon,
// text, and a time.
void drawLogEntryRow(SDL_Renderer* renderer, TTF_Font* font, int x, int y, int width, const LogEntry& entry) {
    bool isTransfer = (entry.kind == LogEntry::Kind::Uploaded || entry.kind == LogEntry::Kind::Downloaded);
    int cy = y + 11;

    switch (entry.kind) {
        case LogEntry::Kind::Uploaded:
            drawArrow(renderer, x + 10, cy, 9, true, receivedColor);
            break;
        case LogEntry::Kind::Downloaded:
            drawArrow(renderer, x + 10, cy, 9, false, sentColor);
            break;
        case LogEntry::Kind::Deleted:
        case LogEntry::Kind::Error:
            drawX(renderer, x + 10, cy, 7, Theme::danger);
            break;
        case LogEntry::Kind::Connected:
            filledCircleColor(renderer, x + 10, cy, 6, packColor(connectedColor));
            break;
        case LogEntry::Kind::Disconnected:
            filledCircleColor(renderer, x + 10, cy, 6, packColor(Theme::textTertiary));
            break;
        case LogEntry::Kind::FolderCreated:
            filledCircleColor(renderer, x + 10, cy, 6, packColor(connectedColor));
            break;
        case LogEntry::Kind::Renamed:
            filledCircleColor(renderer, x + 10, cy, 6, packColor(Theme::textSecondary));
            break;
    }

    std::string timeText = formatClockTime(entry.completedAtMs);
    int rightEdge = x + width;
    // Measured, not guessed — a fixed-width assumption here previously
    // let long size/time strings run into each other.
    int timeW = textWidth(font, timeText);

    if (isTransfer) {
        // No progress bar here — every row in this list already finished
        // (the live transfer, if any, has its own card up top), so a bar
        // pinned at 100% was pure decoration. Dropping it also frees up
        // the ~140px it used to reserve, which the filename can now use
        // instead of running out of room and overlapping the QR panel.
        std::string verb = (entry.kind == LogEntry::Kind::Uploaded) ? "Received " : "Sent ";
        std::string sizeText = formatSize(entry.size);
        int sizeW = textWidth(font, sizeText);
        int gap = 18;

        int timeX = rightEdge - timeW;
        int sizeX = timeX - gap - sizeW;
        int nameMaxWidth = sizeX - gap - (x + 28);

        std::string nameText = truncateToWidth(font, verb + entry.text, nameMaxWidth);
        drawText(renderer, font, nameText, x + 28, y, Theme::textPrimary);
        drawText(renderer, font, sizeText, sizeX, y, Theme::textTertiary);
        drawText(renderer, font, timeText, timeX, y, Theme::textTertiary);
    } else {
        std::string text;
        switch (entry.kind) {
            case LogEntry::Kind::Deleted: text = "Deleted " + entry.text; break;
            case LogEntry::Kind::FolderCreated: text = "Created folder " + entry.text; break;
            case LogEntry::Kind::Renamed: text = "Renamed to " + entry.text; break;
            case LogEntry::Kind::Connected: text = "Connected: " + entry.text; break;
            case LogEntry::Kind::Disconnected: text = "Disconnected: " + entry.text; break;
            case LogEntry::Kind::Error: text = entry.text; break;
            default: break;
        }
        SDL_Color textColor = (entry.kind == LogEntry::Kind::Error) ? Theme::danger : Theme::textSecondary;
        int maxWidth = (rightEdge - timeW - 18) - (x + 28);
        drawText(renderer, font, truncateToWidth(font, text, maxWidth), x + 28, y, textColor);
        drawText(renderer, font, timeText, rightEdge - timeW, y, Theme::textTertiary);
    }
}

}

size_t browseScrollOffset() {
    return g_browseScrollOffset;
}

void beginBrowseDrag() {
    g_browseDragging = true;
}

void endBrowseDrag() {
    g_browseDragging = false;
}

void adjustBrowseScroll(int deltaRows, size_t entryCount) {
    size_t visibleRows = static_cast<size_t>((listBottom - listTop) / rowHeight);
    if (visibleRows == 0) {
        visibleRows = 1;
    }
    if (entryCount <= visibleRows) {
        g_browseScrollOffset = 0;
        return;
    }
    long long maxOffset = static_cast<long long>(entryCount - visibleRows);
    long long newOffset = static_cast<long long>(g_browseScrollOffset) + deltaRows;
    if (newOffset < 0) {
        newOffset = 0;
    } else if (newOffset > maxOffset) {
        newOffset = maxOffset;
    }
    g_browseScrollOffset = static_cast<size_t>(newOffset);
}

std::optional<Screen> headerTabAt(TTF_Font* fontBold, int x, int y) {
    HeaderTabs tabs = computeHeaderTabs(fontBold);
    if (pointInRect(x, y, tabs.server)) {
        return Screen::Server;
    }
    if (pointInRect(x, y, tabs.browse)) {
        return Screen::Browse;
    }
    return std::nullopt;
}

std::optional<FtpRoot> rootCardAt(int x, int y) {
    RootCards cards = computeRootCards();
    if (pointInRect(x, y, cards.sd)) {
        return FtpRoot::Sd;
    }
    if (pointInRect(x, y, cards.nand)) {
        return FtpRoot::Nand;
    }
    if (pointInRect(x, y, cards.games)) {
        return FtpRoot::InstalledGames;
    }
    if (pointInRect(x, y, cards.saves)) {
        return FtpRoot::Saves;
    }
    return std::nullopt;
}

// Grid order: 0=SD, 1=NAND (row 0), 2=Installed Games, 3=Saves (row 1) —
// matches computeRootCards()'s layout and drawServerFrame's rendering order.
FtpRoot rootCardAtIndex(int index) {
    switch (index) {
        case 0: return FtpRoot::Sd;
        case 1: return FtpRoot::Nand;
        case 2: return FtpRoot::InstalledGames;
        default: return FtpRoot::Saves;
    }
}

int rootCardIndexOf(FtpRoot root) {
    switch (root) {
        case FtpRoot::Sd: return 0;
        case FtpRoot::Nand: return 1;
        case FtpRoot::InstalledGames: return 2;
        case FtpRoot::Saves: return 3;
    }
    return 0;
}

int rootCardMoveFocus(int currentIndex, int dCol, int dRow) {
    int col = std::clamp(currentIndex % 2 + dCol, 0, 1);
    int row = std::clamp(currentIndex / 2 + dRow, 0, 1);
    return row * 2 + col;
}

void fillRoundedRect(SDL_Renderer* renderer, SDL_Rect rect, int radius, SDL_Color color) {
    roundedBoxColor(renderer, rect.x, rect.y, rect.x + rect.w, rect.y + rect.h, radius, packColor(color));
}

void drawText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text,
              int x, int y, SDL_Color color, bool centered) {
    if (text.empty() || !font) {
        return;
    }
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) {
        return;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_Rect dst { x, y, surface->w, surface->h };
    if (centered) {
        dst.x -= surface->w / 2;
    }
    SDL_FreeSurface(surface);
    SDL_RenderCopy(renderer, texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
}

std::string formatSize(long long bytes) {
    if (bytes < 0) {
        return "";
    }
    constexpr const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        unit++;
    }
    char buf[32];
    if (unit == 0) {
        std::snprintf(buf, sizeof(buf), "%d %s", static_cast<int>(value), units[unit]);
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f %s", value, units[unit]);
    }
    return buf;
}

std::string formatSpeed(double bytesPerSecond) {
    if (bytesPerSecond < 0) {
        return "";
    }
    constexpr const char* units[] = { "B/s", "KB/s", "MB/s", "GB/s" };
    double value = bytesPerSecond;
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        unit++;
    }
    char buf[32];
    if (unit == 0) {
        std::snprintf(buf, sizeof(buf), "%d %s", static_cast<int>(value), units[unit]);
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f %s", value, units[unit]);
    }
    return buf;
}

// A small "%" pill with a thin battery glyph in front of it — drawn in the
// header so it's visible from both tabs instead of just one.
void drawBatteryStatus(SDL_Renderer* renderer, TTF_Font* font, int rightEdge, int y, int percent, bool charging) {
    std::string text = std::to_string(percent) + "%";
    int textW = textWidth(font, text);

    int bodyW = 24;
    int bodyH = 12;
    int nubW = 2;
    SDL_Rect body { rightEdge - textW - 8 - bodyW - nubW, y, bodyW, bodyH };
    SDL_Rect nub { body.x + body.w, y + 3, nubW, bodyH - 6 };

    SDL_Color fillColor = percent <= 20 ? Theme::danger : Theme::textSecondary;
    roundedRectangleColor(renderer, body.x, body.y, body.x + body.w, body.y + body.h, 3, packColor(Theme::textTertiary));
    fillRoundedRect(renderer, nub, 1, Theme::textTertiary);
    int fillW = std::max(2, (body.w - 4) * percent / 100);
    SDL_Rect fill { body.x + 2, body.y + 2, fillW, body.h - 4 };
    fillRoundedRect(renderer, fill, 1, fillColor);

    if (charging) {
        // Small lightning bolt, just left of the battery glyph.
        Sint16 vx[4] = { static_cast<Sint16>(body.x - 14), static_cast<Sint16>(body.x - 6),
                          static_cast<Sint16>(body.x - 10), static_cast<Sint16>(body.x - 2) };
        Sint16 vy[4] = { static_cast<Sint16>(y + 1), static_cast<Sint16>(y + 5),
                          static_cast<Sint16>(y + 6), static_cast<Sint16>(y + 11) };
        filledPolygonColor(renderer, vx, vy, 4, packColor(connectedColor));
    }

    drawText(renderer, font, text, rightEdge - textW, y - 2, Theme::textSecondary);
}

void drawHeader(SDL_Renderer* renderer, TTF_Font* fontRegular, TTF_Font* fontBold, Screen active,
                const DeviceStatus& status) {
    SDL_Rect header { 0, 0, screenWidth, 100 };
    SDL_SetRenderDrawColor(renderer, Theme::panel.r, Theme::panel.g, Theme::panel.b, 255);
    SDL_RenderFillRect(renderer, &header);
    lineColor(renderer, 0, 100, screenWidth, 100, packColor(Theme::divider));

    drawBoatGlyph(renderer, 54, 50, Theme::accent);
    drawText(renderer, fontBold, "Ferry", 82, 32, Theme::textPrimary);

    HeaderTabs tabs = computeHeaderTabs(fontBold);
    bool serverActive = (active == Screen::Server);
    fillRoundedRect(renderer, tabs.server, tabs.server.h / 2, serverActive ? Theme::accent : Theme::elevated);
    drawText(renderer, fontBold, "Server",
              tabs.server.x + (tabs.server.w - textWidth(fontBold, "Server")) / 2, tabs.server.y + 12,
              serverActive ? Theme::background : Theme::textSecondary);

    bool browseActive = (active == Screen::Browse);
    fillRoundedRect(renderer, tabs.browse, tabs.browse.h / 2, browseActive ? Theme::accent : Theme::elevated);
    drawText(renderer, fontBold, "Browse",
              tabs.browse.x + (tabs.browse.w - textWidth(fontBold, "Browse")) / 2, tabs.browse.y + 12,
              browseActive ? Theme::background : Theme::textSecondary);

    if (status.batteryPercent >= 0) {
        drawBatteryStatus(renderer, fontRegular, screenWidth - 40, 20, status.batteryPercent, status.charging);
    }
    // ZL only does anything on the Server tab (sets/clears the PIN), so it
    // only joins the rest of the button hints there — previously repeated
    // in the Server screen body itself, which is now free for other things.
    std::string hints = "L/R Tab   ZR Theme   - Help   + Exit";
    if (active == Screen::Server) {
        hints = "ZL PIN   \xC2\xB7   " + hints;
    }
    drawTextRight(renderer, fontRegular, hints, screenWidth - 40, 60, Theme::textTertiary);
}

void drawFrame(SDL_Renderer* renderer, TTF_Font* fontRegular, TTF_Font* fontBold,
               const FileBrowser& browser, const std::string& statusMessage, const DeviceStatus& status) {
    SDL_SetRenderDrawColor(renderer, Theme::background.r, Theme::background.g, Theme::background.b, 255);
    SDL_RenderClear(renderer);
    drawBackgroundWaves(renderer);

    drawHeader(renderer, fontRegular, fontBold, Screen::Browse, status);
    drawText(renderer, fontRegular, browser.path(), 40, 108, Theme::textSecondary);
    if (!status.freeSpace.empty()) {
        drawTextRight(renderer, fontRegular, status.freeSpace, screenWidth - 40, 108, Theme::textTertiary);
    }

    const auto& entries = browser.entries();

    if (entries.empty()) {
        drawText(renderer, fontRegular, "Empty folder", screenWidth / 2, listTop + 40,
                  Theme::textTertiary, true);
    }

    // Keeps the selected row inside the visible window, scrolling by the
    // minimum amount needed rather than always realigning to the top —
    // persists across frames so the list doesn't jump around. Namespace-
    // scoped (not function-local) so touch input can map a tap's y
    // position back to an absolute entry index via browseScrollOffset().
    size_t visibleRows = static_cast<size_t>((listBottom - listTop) / rowHeight);
    if (visibleRows == 0) {
        visibleRows = 1;
    }
    size_t selection = browser.selection();
    if (!g_browseDragging) {
        // Mid-drag, the finger is in direct control of the offset —
        // snapping back to the selection every frame would fight it.
        if (selection < g_browseScrollOffset) {
            g_browseScrollOffset = selection;
        }
        if (entries.size() > visibleRows && selection >= g_browseScrollOffset + visibleRows) {
            g_browseScrollOffset = selection - visibleRows + 1;
        }
    }
    if (entries.size() <= visibleRows) {
        g_browseScrollOffset = 0;
    } else if (g_browseScrollOffset + visibleRows > entries.size()) {
        g_browseScrollOffset = entries.size() - visibleRows;
    }

    int y = listTop;
    for (size_t i = g_browseScrollOffset; i < entries.size(); ++i) {
        if (y > listBottom) {
            break;
        }
        const Entry& entry = entries[i];
        bool selected = (i == browser.selection());

        SDL_Rect row { 32, y, screenWidth - 64, rowHeight - 8 };
        if (selected) {
            fillRoundedRect(renderer, row, 14, Theme::accentDim);
        }

        SDL_Rect icon { row.x + 20, row.y + (row.h - 28) / 2, 28, 28 };
        SDL_Texture* iconTex = entry.isParentLink ? nullptr
                                                   : FileIcons::texture(renderer, FileIcons::name(entry.name, entry.isDir));
        if (iconTex) {
            SDL_RenderCopy(renderer, iconTex, nullptr, &icon);
        } else {
            SDL_Color iconColor = entry.isParentLink ? Theme::textTertiary : (entry.isDir ? Theme::accent : Theme::textSecondary);
            fillRoundedRect(renderer, icon, entry.isDir ? 6 : 4, iconColor);
        }

        drawText(renderer, fontRegular, entry.name, icon.x + 44, row.y + (row.h - 22) / 2,
                  entry.isParentLink ? Theme::textSecondary : Theme::textPrimary);

        if (!entry.isDir) {
            std::string sizeText = formatSize(entry.size);
            drawText(renderer, fontRegular, sizeText, row.x + row.w - 24, row.y + (row.h - 22) / 2,
                      Theme::textTertiary, true);
        }

        y += rowHeight;
    }

    SDL_Rect footer { 0, screenHeight - 56, screenWidth, 56 };
    SDL_SetRenderDrawColor(renderer, Theme::panel.r, Theme::panel.g, Theme::panel.b, 255);
    SDL_RenderFillRect(renderer, &footer);
    lineColor(renderer, 0, screenHeight - 56, screenWidth, screenHeight - 56, packColor(Theme::divider));

    const std::string hints = "A Open   B Back   Y Delete   X New Folder";
    drawText(renderer, fontRegular, hints, 48, screenHeight - 40, Theme::textSecondary);

    if (!statusMessage.empty()) {
        drawTextRight(renderer, fontRegular, statusMessage, screenWidth - 48, screenHeight - 40, Theme::danger);
    }
}

void drawServerFrame(SDL_Renderer* renderer, TTF_Font* fontRegular, TTF_Font* fontBold,
                      const std::string& localIp, uint16_t port, size_t clientCount,
                      const std::string& primaryClientIp, const std::vector<TransferInfo>& transfers,
                      const std::deque<LogEntry>& log, const SessionStats& stats, bool pinProtected,
                      const DeviceStatus& status, FtpRoot currentRoot, int cardFocus) {
    SDL_SetRenderDrawColor(renderer, Theme::background.r, Theme::background.g, Theme::background.b, 255);
    SDL_RenderClear(renderer);
    drawBackgroundWaves(renderer);

    drawHeader(renderer, fontRegular, fontBold, Screen::Server, status);

    bool connected = clientCount > 0;
    const int left = serverLeft;
    std::string address = localIp + ":" + std::to_string(port);

    // Not connected: the right half of the screen is a big QR/address
    // panel, so the rest of the content is confined to the left half.
    const int rightPanelX = serverRightPanelX;
    int contentRight = connected ? (screenWidth - left) : (rightPanelX - 20);

    SDL_Color dotColor = connected ? connectedColor : waitingColor;
    SDL_Rect dot { left, 132, 16, 16 };
    int dotCx = dot.x + dot.w / 2;
    int dotCy = dot.y + dot.h / 2;

    if (connected) {
        double phase = static_cast<double>(nowMs() % 1600) / 1600.0;
        int ringRadius = 8 + static_cast<int>(phase * 14);
        Uint8 ringAlpha = static_cast<Uint8>((1.0 - phase) * 140);
        SDL_Color ring = connectedColor;
        ring.a = ringAlpha;
        circleColor(renderer, dotCx, dotCy, ringRadius, packColor(ring));
    }

    fillRoundedRect(renderer, dot, 8, dotColor);

    std::string stateText = !connected ? "Waiting for a connection"
                             : clientCount == 1 ? ("Connected from " + primaryClientIp)
                                                : (std::to_string(clientCount) + " clients connected");
    drawText(renderer, fontBold, stateText, left + 30, 118, connected ? Theme::textPrimary : Theme::textSecondary);

    // While disconnected, the address is already shown prominently under
    // the QR code on the right, so repeating it here was pure redundancy —
    // connected-only now (it's the *only* place the address shows once the
    // QR panel is gone).
    if (connected) {
        std::string addrLine = "This console  \xE2\x80\x94  " + address;
        drawText(renderer, fontRegular, addrLine, left, 168, Theme::textTertiary);

        if (pinProtected) {
            int addrW = textWidth(fontRegular, addrLine);
            int lockX = left + addrW + 26;
            int lockY = 168 + 11;
            SDL_Rect shackle { lockX - 5, lockY - 12, 10, 8 };
            roundedRectangleColor(renderer, shackle.x, shackle.y, shackle.x + shackle.w, shackle.y + shackle.h,
                                  4, packColor(Theme::accent));
            SDL_Rect body { lockX - 7, lockY - 5, 14, 11 };
            fillRoundedRect(renderer, body, 3, Theme::accent);
            drawText(renderer, fontRegular, "PIN protected", lockX + 16, 164, Theme::accent);
        }
    }

    // Shares addrLine's row when connected — the PIN hint moved to the
    // header (see drawHeader), so this slot is now either the "Y
    // Disconnect" hint (connected — an easy-to-miss but important control,
    // same reasoning that used to apply to the PIN hint) or empty. When
    // disconnected there's nothing to put on the left here; the QR panel's
    // subtitle already states PIN status, so this row just carries
    // status.freeSpace, moved up into addrLine's old slot.
    int hintY = connected ? 198 : 168;
    if (connected) {
        drawText(renderer, fontRegular, "Y  Disconnect", left, hintY, Theme::textTertiary);
    }
    if (!status.freeSpace.empty()) {
        drawTextRight(renderer, fontRegular, status.freeSpace, contentRight, hintY, Theme::textTertiary);
    }

    if (!connected) {
        SDL_Rect qrPanel { rightPanelX, 130, screenWidth - left - rightPanelX, (screenHeight - 20) - 130 };
        fillRoundedRect(renderer, qrPanel, 20, Theme::panel);

        int qrSize = 270;
        int qrX = qrPanel.x + (qrPanel.w - qrSize) / 2;
        int qrY = qrPanel.y + 38;
        drawText(renderer, fontRegular, "Scan or enter this address", qrPanel.x + qrPanel.w / 2, qrPanel.y + 14,
                  Theme::textSecondary, true);
        drawQrCode(renderer, qrX, qrY, qrSize, address);
        drawText(renderer, fontBold, address, qrPanel.x + qrPanel.w / 2, qrY + qrSize + 20, Theme::accent, true);
        std::string sub = pinProtected ? "Any FTP client \xC2\xB7 PIN required as the password"
                                        : "Any FTP client \xC2\xB7 anonymous login";
        drawText(renderer, fontRegular, sub, qrPanel.x + qrPanel.w / 2, qrY + qrSize + 44, Theme::textSecondary, true);

        // Root picker sits below the address block, still inside this same
        // panel — see computeRootCards(). The active card's own highlight
        // already communicates which root and permissions are selected, so
        // there's no separate "D-Pad + A, Y, or tap" hint or status badge
        // duplicating that here (the D-Pad binding is documented in Help).
        // "Games" not "Installed Games" on the card itself — the full name
        // doesn't fit this card width at the bold title size and was
        // running past the card's edge; still called "Installed Games"
        // everywhere else (status badge, Help) where there's more room.
        RootCards cards = computeRootCards();
        drawRootCard(renderer, fontBold, fontRegular, cards.sd, "SD Card", "Read \xC2\xB7 Write",
                     currentRoot == FtpRoot::Sd, cardFocus == 0);
        drawRootCard(renderer, fontBold, fontRegular, cards.nand, "NAND", "Read-only",
                     currentRoot == FtpRoot::Nand, cardFocus == 1);
        drawRootCard(renderer, fontBold, fontRegular, cards.games, "Games", "Browse \xC2\xB7 Upload",
                     currentRoot == FtpRoot::InstalledGames, cardFocus == 2);
        drawRootCard(renderer, fontBold, fontRegular, cards.saves, "Saves", "Read \xC2\xB7 Write",
                     currentRoot == FtpRoot::Saves, cardFocus == 3);
    }

    // Once connected, the cards are gone (switching roots requires
    // disconnecting first — see FtpServer::setRoot), so this is the only
    // remaining reminder of which filesystem is mounted. While disconnected
    // the active card's highlight already says the same thing.
    if (connected && currentRoot != FtpRoot::Sd) {
        const char* rootLabel = currentRoot == FtpRoot::Nand             ? "NAND \xC2\xB7 Read-only"
                                 : currentRoot == FtpRoot::InstalledGames ? "Installed Games \xC2\xB7 Browse \xC2\xB7 Upload"
                                                                           : "Saves \xC2\xB7 Read \xC2\xB7 Write";
        drawTextRight(renderer, fontBold, rootLabel, contentRight, 168, Theme::danger);
    }

    int cardTop = 236;
    int y;

    if (connected && !transfers.empty()) {
        // A few clients can be transferring at once — one compact row each,
        // capped so a burst of parallel transfers can't push the activity
        // log off the bottom of the screen.
        constexpr size_t maxShownTransfers = 3;
        size_t shown = std::min(transfers.size(), maxShownTransfers);
        int rowH = 64;
        bool hasOverflow = transfers.size() > shown;
        int cardHeight = 16 + static_cast<int>(shown) * rowH + (hasOverflow ? 26 : 0);
        SDL_Rect card { left, cardTop, contentRight - left, cardHeight };
        fillRoundedRect(renderer, card, 20, Theme::panel);

        int rowY = card.y + 8;
        for (size_t i = 0; i < shown; ++i) {
            const TransferInfo& t = transfers[i];
            bool isUpload = t.isUpload;
            SDL_Color dirColor = isUpload ? receivedColor : sentColor;
            drawArrow(renderer, card.x + 44, rowY + 18, 11, isUpload, dirColor);

            std::string verb = isUpload ? "Receiving " : "Sending ";
            drawText(renderer, fontBold, verb + t.name, card.x + 80, rowY, Theme::textPrimary);
            if (!t.directory.empty()) {
                drawTextRight(renderer, fontRegular, "in " + t.directory, card.x + card.w - 24, rowY + 2,
                              Theme::textTertiary);
            }

            double speed = t.speedBps; // smoothed server-side — see sampleSpeed() in ftp_server.cpp

            SDL_Rect barRect { card.x + 80, rowY + 28, card.w - 120, 8 };
            if (t.bytesTotal > 0) {
                double fraction = static_cast<double>(t.bytesDone) / static_cast<double>(t.bytesTotal);
                drawProgressBar(renderer, barRect, fraction, dirColor);
                std::string stats2 = formatSize(t.bytesDone) + " / " + formatSize(t.bytesTotal) +
                                      "   \xC2\xB7   " + formatSpeed(speed) +
                                      "   \xC2\xB7   " + std::to_string(static_cast<int>(fraction * 100)) + "%";
                drawText(renderer, fontRegular, stats2, card.x + 80, rowY + 42, Theme::textSecondary);
            } else {
                drawIndeterminateBar(renderer, barRect, dirColor);
                std::string stats2 = formatSize(t.bytesDone) + " received so far   \xC2\xB7   " + formatSpeed(speed);
                drawText(renderer, fontRegular, stats2, card.x + 80, rowY + 42, Theme::textSecondary);
            }

            rowY += rowH;
        }
        if (hasOverflow) {
            std::string more = "+" + std::to_string(transfers.size() - shown) + " more transferring";
            drawText(renderer, fontRegular, more, card.x + 80, rowY + 2, Theme::textTertiary);
        }

        y = card.y + card.h + 30;
    } else if (connected) {
        int cardHeight = 100;
        SDL_Rect card { left, cardTop, contentRight - left, cardHeight };
        fillRoundedRect(renderer, card, 20, Theme::panel);

        int total = stats.filesUp + stats.filesDown;
        std::string statsLine;
        if (total == 0) {
            statsLine = "No transfers yet this session";
        } else {
            statsLine = std::to_string(stats.filesUp) + " received  \xC2\xB7  " +
                        std::to_string(stats.filesDown) + " sent  \xC2\xB7  " +
                        formatSize(stats.bytesUp + stats.bytesDown) + " total";
        }
        drawText(renderer, fontRegular, statsLine, card.x + card.w / 2, card.y + card.h / 2 - 12,
                  Theme::textSecondary, true);

        y = card.y + card.h + 30;
    } else {
        y = cardTop;
    }

    drawText(renderer, fontRegular, "Recent activity", left, y, Theme::textTertiary);
    y += 40;

    if (log.empty()) {
        drawText(renderer, fontRegular, "Nothing yet", left + 4, y, Theme::textTertiary);
    }

    int rowWidth = contentRight - left;
    for (auto it = log.rbegin(); it != log.rend(); ++it) {
        drawLogEntryRow(renderer, fontRegular, left, y, rowWidth, *it);
        y += 34;
        if (y > listBottom) {
            break;
        }
    }
}

void drawModal(SDL_Renderer* renderer, TTF_Font* fontRegular, TTF_Font* fontBold,
               const std::string& title, const std::string& body, const std::string& hint) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
    SDL_Rect overlay { 0, 0, screenWidth, screenHeight };
    SDL_RenderFillRect(renderer, &overlay);

    int cardWidth = 560;
    int cardHeight = 220;
    SDL_Rect card {
        (screenWidth - cardWidth) / 2,
        (screenHeight - cardHeight) / 2,
        cardWidth,
        cardHeight
    };
    fillRoundedRect(renderer, card, 20, Theme::elevated);

    drawText(renderer, fontBold, title, screenWidth / 2, card.y + 36, Theme::textPrimary, true);
    drawText(renderer, fontRegular, body, screenWidth / 2, card.y + 92, Theme::textSecondary, true);
    drawText(renderer, fontRegular, hint, screenWidth / 2, card.y + cardHeight - 46, Theme::textTertiary, true);
}

void drawHelp(SDL_Renderer* renderer, TTF_Font* fontRegular, TTF_Font* fontBold, Screen active) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_Rect overlay { 0, 0, screenWidth, screenHeight };
    SDL_RenderFillRect(renderer, &overlay);

    // Wide enough that the longest control-row description (key badge +
    // text) never gets close to the right edge, and short enough on the
    // vertical rhythm between rows/sections that everything — including
    // the closing log-path line — actually ends up inside the card
    // instead of spilling out past its bottom edge.
    int cardWidth = 860;
    int cardHeight = 706;
    SDL_Rect card {
        (screenWidth - cardWidth) / 2,
        (screenHeight - cardHeight) / 2,
        cardWidth,
        cardHeight
    };
    fillRoundedRect(renderer, card, 24, Theme::elevated);
    int contentX = card.x + 44;

    int y = card.y + 32;
    drawText(renderer, fontBold, "Help", contentX, y, Theme::textPrimary);
    y += 40;

    drawText(renderer, fontRegular,
              "Ferry runs a small FTP server on this console \xE2\x80\x94 point any FTP client",
              contentX, y, Theme::textSecondary);
    y += 24;
    drawText(renderer, fontRegular,
              "at the address on the Server tab (anonymous, or your PIN if set).",
              contentX, y, Theme::textSecondary);
    y += 34;

    drawText(renderer, fontBold, "SERVER TAB", contentX, y, Theme::textTertiary);
    y += 32;
    drawControlRow(renderer, fontRegular, contentX, y, "ZL", "Set or clear the PIN clients must send as their password");
    y += 34;
    drawControlRow(renderer, fontRegular, contentX, y, "D-Pad + A, or tap", "Select SD / NAND / Installed Games / Saves");
    y += 34;
    drawControlRow(renderer, fontRegular, contentX, y, "Y",
                   "Cycle SD / NAND / Installed Games / Saves \xE2\x80\x94 or disconnect, once connected");
    y += 34;
    drawControlRow(renderer, fontRegular, contentX, y, "L / R", "Switch to the Browse tab");
    y += 42;

    drawText(renderer, fontBold, "BROWSE TAB", contentX, y, Theme::textTertiary);
    y += 32;
    drawControlRow(renderer, fontRegular, contentX, y, "D-Pad / Stick", "Move selection, or tap a row on the touch screen");
    y += 34;
    drawControlRow(renderer, fontRegular, contentX, y, "A", "Open the selected folder");
    y += 34;
    drawControlRow(renderer, fontRegular, contentX, y, "B", "Go up one level");
    y += 34;
    drawControlRow(renderer, fontRegular, contentX, y, "Y", "Delete the selection (with confirmation)");
    y += 34;
    drawControlRow(renderer, fontRegular, contentX, y, "X", "Create a new folder");
    y += 42;

    drawText(renderer, fontBold, "ANYTIME", contentX, y, Theme::textTertiary);
    y += 32;
    drawControlRow(renderer, fontRegular, contentX, y, "ZR", "Switch between light and dark theme");
    y += 34;
    drawControlRow(renderer, fontRegular, contentX, y, "-", "Open or close this help");
    y += 34;
    drawControlRow(renderer, fontRegular, contentX, y, "+", "Exit Ferry");
    y += 38;

    drawText(renderer, fontRegular, "Every event is also logged to sdmc:/switch/Ferry/log.txt",
              contentX, y, Theme::textTertiary);

    (void)active;
}

}
