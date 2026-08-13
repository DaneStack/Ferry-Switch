#include <switch.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include <sys/statvfs.h>

#include <cmath>
#include <cstring>
#include <string>

#include "file_browser.hpp"
#include "file_icons.hpp"
#include "ftp_server.hpp"
#include "theme.hpp"
#include "ui.hpp"

namespace {

// devkitPro's SDL2 port reports the Switch's built-in controller as a raw
// joystick with fixed button indices that match the physical Nintendo
// labels directly (A=0, B=1, ...). Going through the SDL_GameController
// abstraction instead remaps A/B to the Xbox-style *positional* layout,
// which swaps them relative to how they're printed on the controller —
// so raw SDL_Joystick indices are used here instead.
namespace Btn {
    constexpr int A = 0;
    constexpr int B = 1;
    constexpr int X = 2;
    constexpr int Y = 3;
    constexpr int L = 6;
    constexpr int R = 7;
    constexpr int ZL = 8;
    constexpr int ZR = 9;
    constexpr int Plus = 10;
    constexpr int Minus = 11;
    // Left/Right aren't exercised anywhere else in this app yet, so these
    // are inferred from the established Up/Down pattern (13/15, 17/19 —
    // each pair straddling an even "the other axis" slot) rather than
    // independently confirmed on hardware. Double-check first if Server-tab
    // card navigation turns out to move the wrong way.
    constexpr int DpadLeft = 12;
    constexpr int DpadUp = 13;
    constexpr int DpadRight = 14;
    constexpr int DpadDown = 15;
    constexpr int StickLeft = 16;
    constexpr int StickUp = 17;
    constexpr int StickRight = 18;
    constexpr int StickDown = 19;
}

constexpr uint16_t ftpPort = 5000;
constexpr Uint32 navRepeatDelayMs = 350;
constexpr Uint32 navRepeatIntervalMs = 110;

enum class Screen {
    Browse,
    Server,
};

enum class Mode {
    Browsing,
    ConfirmDelete,
    ConfirmDisconnect,
};

// Returns false if the user cancelled the keyboard (in which case `out` is
// left untouched) — distinct from submitting an empty string, which the
// PIN prompt below treats as "clear the PIN" rather than "no change".
bool readSwkbdText(const char* guide, std::string& out) {
    SwkbdConfig kbd;
    char buffer[256] = { 0 };

    if (R_FAILED(swkbdCreate(&kbd, 0))) {
        return false;
    }

    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetStringLenMax(&kbd, sizeof(buffer) - 1);

    bool ok = R_SUCCEEDED(swkbdShow(&kbd, buffer, sizeof(buffer)));
    swkbdClose(&kbd);
    if (ok) {
        out = buffer;
    }
    return ok;
}

std::string freeSpaceText() {
    struct statvfs st{};
    if (statvfs("sdmc:/", &st) != 0) {
        return "";
    }
    unsigned long long freeBytes = static_cast<unsigned long long>(st.f_bavail) * st.f_frsize;
    unsigned long long totalBytes = static_cast<unsigned long long>(st.f_blocks) * st.f_frsize;
    return ui::formatSize(static_cast<long long>(freeBytes)) + " / " +
           ui::formatSize(static_cast<long long>(totalBytes));
}

// Battery/free-space don't change fast enough to need a fresh psm IPC call
// and statvfs() every single frame (up to 60/s) — refreshed on a 2s
// throttle instead, same idea as FtpServer's own local-IP refresh.
ui::DeviceStatus g_deviceStatus;
Uint32 g_lastStatusRefreshMs = 0;

const ui::DeviceStatus& currentDeviceStatus() {
    Uint32 now = SDL_GetTicks();
    if (now - g_lastStatusRefreshMs < 2000 && g_lastStatusRefreshMs != 0) {
        return g_deviceStatus;
    }
    g_lastStatusRefreshMs = now;

    u32 percent = 0;
    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&percent))) {
        g_deviceStatus.batteryPercent = static_cast<int>(percent);
    }
    PsmChargerType charger;
    if (R_SUCCEEDED(psmGetChargerType(&charger))) {
        g_deviceStatus.charging = (charger != PsmChargerType_Unconnected);
    }
    g_deviceStatus.freeSpace = freeSpaceText();
    return g_deviceStatus;
}

}

int main(int argc, char** argv) {
    plInitialize(PlServiceType_User);
    // libnx's stock socket buffers are small — fine for control-channel
    // traffic, but this app's whole job is moving bulk file data, and the
    // FTP data connection only gets drained once per poll() tick (see
    // stepTransfer()'s budgetMs). A too-small kernel-side TCP buffer fills
    // up between drains and throttles the other side via TCP flow control
    // regardless of how fast our own read/write loop is — this is a
    // leading suspect for transfers (both directions, not just the ncm
    // content-download path) topping out far below what DBI reportedly
    // achieves on the same hardware. Same reasoning applies to send: a
    // too-small tx buffer means our own send() calls return short/blocked
    // sooner than they'd need to.
    SocketInitConfig socketConfig = *socketGetDefaultInitConfig();
    socketConfig.tcp_tx_buf_size = 1 * 1024 * 1024;
    socketConfig.tcp_rx_buf_size = 1 * 1024 * 1024;
    socketConfig.tcp_tx_buf_max_size = 4 * 1024 * 1024;
    socketConfig.tcp_rx_buf_max_size = 4 * 1024 * 1024;
    socketInitialize(&socketConfig);

    // This app's whole point is sitting idle waiting for a connection —
    // without this, the console auto-sleeps after a few minutes of no
    // button presses and the FTP server goes down with it.
    appletSetAutoSleepDisabled(true);
    Theme::loadFromConfig();
    romfsInit(); // mounts romfs:/ — bundled file-type icons live there
    psmInitialize(); // battery percentage/charging state for the header
    nsInitialize(); // installed-title listing for the "Installed Games" root
    accountInitialize(AccountServiceType_Application); // resolves a user for save-data mounting
    // Every ncm call (content-meta queries, content reads) was silently
    // failing without this — unlike ns/account/psm, ncm isn't lazily
    // initialized on first use, so every ncmOpen*/ncmContentMetaDatabase*
    // call was returning a failure Result and getting swallowed by this
    // codebase's fail-closed "treat as empty" handling. This is what the
    // diag_seen0_matched0_empty0 report turned out to actually mean: not a
    // filtering or storage-location bug (the two things tried before this),
    // but the service handle never existing in the first place.
    ncmInitialize();

    PlFontData standardFont;
    bool haveFont = R_SUCCEEDED(plGetSharedFontByType(&standardFont, PlSharedFontType_Standard));

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
    TTF_Init();
    IMG_Init(IMG_INIT_PNG);

    SDL_Window* window = SDL_CreateWindow("Ferry", 0, 0, ui::screenWidth, ui::screenHeight, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    TTF_Font* fontRegular = nullptr;
    TTF_Font* fontBold = nullptr;
    if (haveFont) {
        fontRegular = TTF_OpenFontRW(SDL_RWFromConstMem(standardFont.address, standardFont.size), 1, 22);
        fontBold = TTF_OpenFontRW(SDL_RWFromConstMem(standardFont.address, standardFont.size), 1, 30);
    }

    SDL_Joystick* joystick = SDL_NumJoysticks() > 0 ? SDL_JoystickOpen(0) : nullptr;

    FileBrowser browser("sdmc:/");
    Screen screen = Screen::Server;
    Mode mode = Mode::Browsing;
    std::string statusMessage;
    bool showHelp = false;

    FtpServer ftpServer;
    ftpServer.start(ftpPort);

    // Which of the 4 root-picker cards has D-Pad/stick focus (separate from
    // which one is actually active) — kept in sync with the active root
    // whenever the Server tab is (re)entered, see below.
    int serverCardFocus = ui::rootCardIndexOf(ftpServer.root());

    bool navHeld = false;
    int navDirection = 0;
    Uint32 nextRepeatTime = 0;

    // Browse-list touch: a finger-down on a row starts "tracking"; if it
    // moves past a small threshold it becomes a drag-to-scroll instead of
    // a tap, so a single touch can either scroll or select/open but never
    // both. Tap activation needs two taps on the same row within a short
    // window — a single tap used to open folders immediately, which made
    // it too easy to dive into the wrong folder by accident.
    bool touchTracking = false;
    bool touchDragging = false;
    float touchTotalDy = 0.0f;
    float touchRowAccum = 0.0f;
    size_t lastTapRow = static_cast<size_t>(-1);
    Uint32 lastTapTimeMs = 0;
    constexpr float dragThresholdNorm = 10.0f / ui::screenHeight;
    constexpr Uint32 doubleTapWindowMs = 400;

    bool running = true;
    while (appletMainLoop() && running) {
        ftpServer.poll();

        // The confirm-disconnect modal only makes sense while a client is
        // actually connected — if it dropped on its own while the modal was
        // up (rather than via the Y/A flow below), don't leave it stuck on
        // screen guarding a connection that's already gone.
        if (mode == Mode::ConfirmDisconnect && ftpServer.clientCount() == 0) {
            mode = Mode::Browsing;
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_JOYBUTTONDOWN) {
                int button = event.jbutton.button;

                if (button == Btn::Plus) {
                    running = false;
                    continue;
                }
                if (showHelp) {
                    showHelp = false;
                    continue;
                }
                if (button == Btn::Minus) {
                    showHelp = true;
                    continue;
                }
                if (button == Btn::ZL) {
                    std::string newPin;
                    const char* guide = ftpServer.pin().empty()
                        ? "Set a PIN clients must send as their FTP password (leave empty for open access)"
                        : "Change the PIN, or leave empty to remove it";
                    if (readSwkbdText(guide, newPin)) {
                        ftpServer.setPin(newPin);
                    }
                    continue;
                }
                if (button == Btn::ZR) {
                    Theme::toggleMode();
                    continue;
                }
                if (button == Btn::L || button == Btn::R) {
                    if (touchTracking) {
                        // Shouldn't normally overlap with a touch gesture,
                        // but if it ever does, don't leave browseDragging
                        // stuck on forever with no FINGERUP left to clear it.
                        touchTracking = false;
                        touchDragging = false;
                        ui::endBrowseDrag();
                    }
                    screen = (screen == Screen::Browse) ? Screen::Server : Screen::Browse;
                    mode = Mode::Browsing;
                    if (screen == Screen::Server) {
                        serverCardFocus = ui::rootCardIndexOf(ftpServer.root());
                    }
                    continue;
                }
                if (screen == Screen::Server && ftpServer.clientCount() == 0) {
                    if (button == Btn::Y) {
                        FtpRoot next;
                        switch (ftpServer.root()) {
                            case FtpRoot::Sd: next = FtpRoot::Nand; break;
                            case FtpRoot::Nand: next = FtpRoot::InstalledGames; break;
                            case FtpRoot::InstalledGames: next = FtpRoot::Saves; break;
                            default: next = FtpRoot::Sd; break;
                        }
                        ftpServer.setRoot(next);
                        serverCardFocus = ui::rootCardIndexOf(ftpServer.root());
                        continue;
                    }
                    if (button == Btn::DpadLeft || button == Btn::StickLeft) {
                        serverCardFocus = ui::rootCardMoveFocus(serverCardFocus, -1, 0);
                        continue;
                    }
                    if (button == Btn::DpadRight || button == Btn::StickRight) {
                        serverCardFocus = ui::rootCardMoveFocus(serverCardFocus, 1, 0);
                        continue;
                    }
                    if (button == Btn::DpadUp || button == Btn::StickUp) {
                        serverCardFocus = ui::rootCardMoveFocus(serverCardFocus, 0, -1);
                        continue;
                    }
                    if (button == Btn::DpadDown || button == Btn::StickDown) {
                        serverCardFocus = ui::rootCardMoveFocus(serverCardFocus, 0, 1);
                        continue;
                    }
                    if (button == Btn::A) {
                        ftpServer.setRoot(ui::rootCardAtIndex(serverCardFocus));
                        serverCardFocus = ui::rootCardIndexOf(ftpServer.root());
                        continue;
                    }
                }
                if (screen == Screen::Server && ftpServer.clientCount() > 0) {
                    if (mode == Mode::ConfirmDisconnect) {
                        if (button == Btn::A) {
                            ftpServer.disconnectAll();
                            mode = Mode::Browsing;
                        } else if (button == Btn::B) {
                            mode = Mode::Browsing;
                        }
                        continue;
                    }
                    if (button == Btn::Y) {
                        mode = Mode::ConfirmDisconnect;
                        continue;
                    }
                }
                if (screen != Screen::Browse) {
                    continue;
                }

                statusMessage.clear();

                if (mode == Mode::ConfirmDelete) {
                    if (button == Btn::A) {
                        if (!browser.deleteSelected()) {
                            statusMessage = "Could not delete";
                        }
                        mode = Mode::Browsing;
                    } else if (button == Btn::B) {
                        mode = Mode::Browsing;
                    }
                    continue;
                }

                switch (button) {
                    case Btn::A:
                        browser.enterSelected();
                        break;
                    case Btn::B:
                        browser.goUp();
                        break;
                    case Btn::Y: {
                        const Entry* selected = browser.selectedEntry();
                        if (selected && !selected->isParentLink) {
                            mode = Mode::ConfirmDelete;
                        }
                        break;
                    }
                    case Btn::X: {
                        std::string name;
                        if (readSwkbdText("New folder name", name) && !name.empty()) {
                            if (!browser.createFolder(name)) {
                                statusMessage = "Could not create folder";
                            }
                        }
                        break;
                    }
                    default:
                        break;
                }
            } else if (event.type == SDL_FINGERDOWN) {
                int touchX = static_cast<int>(event.tfinger.x * ui::screenWidth);
                int touchY = static_cast<int>(event.tfinger.y * ui::screenHeight);

                if (!showHelp) {
                    if (auto tapped = ui::headerTabAt(fontBold, touchX, touchY)) {
                        screen = (*tapped == ui::Screen::Browse) ? Screen::Browse : Screen::Server;
                        mode = Mode::Browsing;
                        continue;
                    }
                }

                if (!showHelp && screen == Screen::Server && ftpServer.clientCount() == 0) {
                    if (auto tapped = ui::rootCardAt(touchX, touchY)) {
                        ftpServer.setRoot(*tapped);
                        serverCardFocus = ui::rootCardIndexOf(ftpServer.root());
                    }
                }

                if (!showHelp && screen == Screen::Browse && mode == Mode::Browsing &&
                    touchY >= ui::listTop && touchY <= ui::listBottom) {
                    touchTracking = true;
                    touchDragging = false;
                    touchTotalDy = 0.0f;
                    touchRowAccum = 0.0f;
                    ui::beginBrowseDrag();
                }
            } else if (event.type == SDL_FINGERMOTION) {
                if (touchTracking) {
                    touchTotalDy += event.tfinger.dy;
                    if (!touchDragging && std::fabs(touchTotalDy) > dragThresholdNorm) {
                        touchDragging = true;
                    }
                    if (touchDragging) {
                        // Content follows the finger: dragging down (dy>0)
                        // reveals earlier rows, so the offset moves the
                        // opposite way from the raw delta.
                        touchRowAccum += -(event.tfinger.dy * ui::screenHeight) / static_cast<float>(ui::rowHeight);
                        int wholeRows = static_cast<int>(touchRowAccum);
                        if (wholeRows != 0) {
                            ui::adjustBrowseScroll(wholeRows, browser.entries().size());
                            touchRowAccum -= static_cast<float>(wholeRows);
                        }
                    }
                }
            } else if (event.type == SDL_FINGERUP) {
                if (touchTracking) {
                    if (!touchDragging) {
                        int touchY = static_cast<int>(event.tfinger.y * ui::screenHeight);
                        if (touchY >= ui::listTop && touchY <= ui::listBottom) {
                            size_t rowIndex = ui::browseScrollOffset() +
                                static_cast<size_t>((touchY - ui::listTop) / ui::rowHeight);
                            const auto& entries = browser.entries();
                            if (rowIndex < entries.size()) {
                                Uint32 now = SDL_GetTicks();
                                bool isDoubleTap = (rowIndex == lastTapRow) && (now - lastTapTimeMs < doubleTapWindowMs);
                                browser.setSelection(rowIndex);
                                if (isDoubleTap) {
                                    browser.enterSelected();
                                    lastTapRow = static_cast<size_t>(-1);
                                } else {
                                    lastTapRow = rowIndex;
                                    lastTapTimeMs = now;
                                }
                            }
                        }
                    } else {
                        // The drag moved the scroll offset but not the
                        // selection — re-enabling the "keep selection in
                        // view" auto-scroll next frame (via endBrowseDrag()
                        // below) would otherwise immediately snap the list
                        // back to wherever the selection still is. Pull the
                        // selection into the window the drag just settled
                        // on instead, so nothing fights the finger's result.
                        size_t visibleRows = static_cast<size_t>((ui::listBottom - ui::listTop) / ui::rowHeight);
                        if (visibleRows == 0) {
                            visibleRows = 1;
                        }
                        size_t offset = ui::browseScrollOffset();
                        size_t sel = browser.selection();
                        if (sel < offset) {
                            browser.setSelection(offset);
                        } else if (sel >= offset + visibleRows) {
                            browser.setSelection(offset + visibleRows - 1);
                        }
                    }
                    touchTracking = false;
                    touchDragging = false;
                    ui::endBrowseDrag();
                }
            }
        }

        if (!showHelp && screen == Screen::Browse && mode == Mode::Browsing && joystick) {
            bool up = SDL_JoystickGetButton(joystick, Btn::DpadUp) || SDL_JoystickGetButton(joystick, Btn::StickUp);
            bool down = SDL_JoystickGetButton(joystick, Btn::DpadDown) || SDL_JoystickGetButton(joystick, Btn::StickDown);
            Uint32 now = SDL_GetTicks();

            if (up || down) {
                int direction = up ? -1 : 1;
                if (!navHeld || direction != navDirection) {
                    browser.moveSelection(direction);
                    navHeld = true;
                    navDirection = direction;
                    nextRepeatTime = now + navRepeatDelayMs;
                } else if (now >= nextRepeatTime) {
                    browser.moveSelection(navDirection);
                    nextRepeatTime = now + navRepeatIntervalMs;
                }
            } else {
                navHeld = false;
            }
        }

        ui::Screen uiScreen = (screen == Screen::Browse) ? ui::Screen::Browse : ui::Screen::Server;

        if (screen == Screen::Browse) {
            ui::drawFrame(renderer, fontRegular, fontBold, browser, statusMessage, currentDeviceStatus());
            if (mode == Mode::ConfirmDelete) {
                const Entry* entry = browser.selectedEntry();
                std::string body = entry ? ("Delete \"" + entry->name + "\"?") : "";
                ui::drawModal(renderer, fontRegular, fontBold, "Delete", body, "A Confirm   B Cancel");
            }
        } else {
            ui::drawServerFrame(renderer, fontRegular, fontBold, ftpServer.localIp(), ftpServer.port(),
                                 ftpServer.clientCount(), ftpServer.primaryClientIp(), ftpServer.activeTransfers(),
                                 ftpServer.log(), ftpServer.stats(), !ftpServer.pin().empty(), currentDeviceStatus(),
                                 ftpServer.root(), serverCardFocus);
            if (mode == Mode::ConfirmDisconnect) {
                std::string body = ftpServer.clientCount() == 1
                    ? ("Disconnect " + ftpServer.primaryClientIp() + "?")
                    : ("Disconnect all " + std::to_string(ftpServer.clientCount()) + " clients?");
                ui::drawModal(renderer, fontRegular, fontBold, "Disconnect", body, "A Confirm   B Cancel");
            }
        }

        if (showHelp) {
            ui::drawHelp(renderer, fontRegular, fontBold, uiScreen);
        }

        SDL_RenderPresent(renderer);
    }

    appletSetAutoSleepDisabled(false);
    ftpServer.stop();

    if (joystick) {
        SDL_JoystickClose(joystick);
    }
    if (fontRegular) {
        TTF_CloseFont(fontRegular);
    }
    if (fontBold) {
        TTF_CloseFont(fontBold);
    }
    FileIcons::releaseAll(); // textures must go before the renderer that owns them
    TTF_Quit();
    IMG_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    socketExit();
    romfsExit();
    psmExit();
    nsExit();
    accountExit();
    ncmExit();
    plExit();

    return 0;
}
