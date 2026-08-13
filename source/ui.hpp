#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "file_browser.hpp"
#include "ftp_server.hpp"

namespace ui {

constexpr int screenWidth = 1280;
constexpr int screenHeight = 720;

constexpr int rowHeight = 64;
constexpr int listTop = 140;
constexpr int listBottom = screenHeight - 70;

enum class Screen {
    Browse,
    Server,
};

// Device-wide info shown in the header (battery) and per-screen (free
// space) — bundled together since both call sites (drawFrame,
// drawServerFrame) need the same data and this is one less parameter to
// keep in sync if more status fields show up later.
struct DeviceStatus {
    int batteryPercent = -1; // -1 = unknown/unavailable, hides the glyph
    bool charging = false;
    std::string freeSpace; // empty = don't show
};

void fillRoundedRect(SDL_Renderer* renderer, SDL_Rect rect, int radius, SDL_Color color);

void drawText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text,
              int x, int y, SDL_Color color, bool centered = false);

std::string formatSize(long long bytes);
std::string formatSpeed(double bytesPerSecond);

void drawHeader(SDL_Renderer* renderer, TTF_Font* fontRegular, TTF_Font* fontBold, Screen active,
                const DeviceStatus& status);

// Which tab pill (if any) contains this point — for touch. Uses the exact
// same layout math as drawHeader so the hitbox always matches what's on
// screen, even if the tab labels or their surrounding padding change.
std::optional<Screen> headerTabAt(TTF_Font* fontBold, int x, int y);

// Which root-picker card (if any) contains this point — same idea as
// headerTabAt, for the SD/NAND cards on the disconnected Server screen.
std::optional<FtpRoot> rootCardAt(int x, int y);

// Root-picker card layout, exposed so main.cpp's D-Pad/stick handling can
// move focus across the 2x2 grid without duplicating the arrangement.
FtpRoot rootCardAtIndex(int index);
int rootCardIndexOf(FtpRoot root);
int rootCardMoveFocus(int currentIndex, int dCol, int dRow);

// Index of the first entry currently visible in the Browse list — set by
// the last drawFrame() call. Lets touch input map a tap's y position back
// to an absolute entry index without duplicating the scroll math.
size_t browseScrollOffset();

// While a finger-drag is in progress, drawFrame()'s usual "keep the
// selection in view" auto-scroll is suspended — otherwise it would fight
// the drag, snapping back to the selected row every frame. D-Pad/stick
// navigation doesn't call these and keeps behaving exactly as before.
void beginBrowseDrag();
void endBrowseDrag();
void adjustBrowseScroll(int deltaRows, size_t entryCount);

void drawFrame(SDL_Renderer* renderer, TTF_Font* fontRegular, TTF_Font* fontBold,
               const FileBrowser& browser, const std::string& statusMessage, const DeviceStatus& status);

void drawModal(SDL_Renderer* renderer, TTF_Font* fontRegular, TTF_Font* fontBold,
               const std::string& title, const std::string& body, const std::string& hint);

void drawServerFrame(SDL_Renderer* renderer, TTF_Font* fontRegular, TTF_Font* fontBold,
                      const std::string& localIp, uint16_t port, size_t clientCount,
                      const std::string& primaryClientIp, const std::vector<TransferInfo>& transfers,
                      const std::deque<LogEntry>& log, const SessionStats& stats, bool pinProtected,
                      const DeviceStatus& status, FtpRoot currentRoot, int cardFocus);

void drawHelp(SDL_Renderer* renderer, TTF_Font* fontRegular, TTF_Font* fontBold, Screen active);

}
