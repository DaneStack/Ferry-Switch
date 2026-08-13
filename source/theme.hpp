#pragma once

#include <SDL2/SDL.h>

namespace Theme {

enum class Mode {
    Dark,
    Light,
};

// Mutable, not constexpr — reassigned by setMode()/toggleMode(). Kept as
// bare `extern SDL_Color` (not functions) so every existing `Theme::xxx`
// reference throughout ui.cpp keeps working unchanged; only this header
// and theme.cpp needed to change.
extern SDL_Color background;
extern SDL_Color panel;
extern SDL_Color elevated;
extern SDL_Color divider;

extern SDL_Color accent;
extern SDL_Color accentDim;
extern SDL_Color danger;

extern SDL_Color textPrimary;
extern SDL_Color textSecondary;
extern SDL_Color textTertiary;

Mode mode();
void setMode(Mode m);
void toggleMode();

// Reads sdmc:/switch/Ferry/config.txt if present; call once at startup.
void loadFromConfig();

}
