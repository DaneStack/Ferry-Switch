#include "theme.hpp"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace Theme {

namespace {

constexpr SDL_Color darkBackground   { 11,  15,  20, 255 };
constexpr SDL_Color darkPanel        { 18,  24,  31, 255 };
constexpr SDL_Color darkElevated     { 23,  30,  39, 255 };
constexpr SDL_Color darkDivider      { 35,  43,  53, 255 };
constexpr SDL_Color darkDanger       { 255, 107, 91, 255 };
constexpr SDL_Color darkTextPrimary  { 234, 242, 242, 255 };
constexpr SDL_Color darkTextSecondary{ 143, 163, 168, 255 };
constexpr SDL_Color darkTextTertiary {  92, 110, 115, 255 };

constexpr SDL_Color lightBackground   { 238, 241, 242, 255 };
constexpr SDL_Color lightPanel        { 247, 249, 250, 255 };
constexpr SDL_Color lightElevated     { 255, 255, 255, 255 };
constexpr SDL_Color lightDivider      { 216, 222, 225, 255 };
constexpr SDL_Color lightDanger       { 229, 73, 58, 255 };
constexpr SDL_Color lightTextPrimary  { 16, 22, 27, 255 };
constexpr SDL_Color lightTextSecondary{ 82, 97, 107, 255 };
constexpr SDL_Color lightTextTertiary { 139, 154, 161, 255 };

// The teal accent is the one constant across both themes — it's the
// app's identity color, not something that should flip with appearance.
constexpr SDL_Color accentColor { 47, 217, 196, 255 };
constexpr SDL_Color accentDimDark  { 47, 217, 196, 40 };
constexpr SDL_Color accentDimLight { 47, 217, 196, 60 };

Mode g_mode = Mode::Dark;

void apply(Mode m) {
    g_mode = m;
    bool dark = (m == Mode::Dark);
    background = dark ? darkBackground : lightBackground;
    panel = dark ? darkPanel : lightPanel;
    elevated = dark ? darkElevated : lightElevated;
    divider = dark ? darkDivider : lightDivider;
    accent = accentColor;
    accentDim = dark ? accentDimDark : accentDimLight;
    danger = dark ? darkDanger : lightDanger;
    textPrimary = dark ? darkTextPrimary : lightTextPrimary;
    textSecondary = dark ? darkTextSecondary : lightTextSecondary;
    textTertiary = dark ? darkTextTertiary : lightTextTertiary;
}

void saveToConfig() {
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/Ferry", 0777);
    FILE* f = fopen("sdmc:/switch/Ferry/config.txt", "w");
    if (!f) {
        return;
    }
    std::fprintf(f, "theme=%s\n", g_mode == Mode::Dark ? "dark" : "light");
    fclose(f);
}

}

SDL_Color background;
SDL_Color panel;
SDL_Color elevated;
SDL_Color divider;
SDL_Color accent;
SDL_Color accentDim;
SDL_Color danger;
SDL_Color textPrimary;
SDL_Color textSecondary;
SDL_Color textTertiary;

Mode mode() {
    return g_mode;
}

void setMode(Mode m) {
    apply(m);
    saveToConfig();
}

void toggleMode() {
    setMode(g_mode == Mode::Dark ? Mode::Light : Mode::Dark);
}

void loadFromConfig() {
    apply(Mode::Dark);
    FILE* f = fopen("sdmc:/switch/Ferry/config.txt", "r");
    if (!f) {
        return;
    }
    char line[128];
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "theme=", 6) == 0) {
            if (std::strncmp(line + 6, "light", 5) == 0) {
                apply(Mode::Light);
            } else {
                apply(Mode::Dark);
            }
            break;
        }
    }
    fclose(f);
}

}
