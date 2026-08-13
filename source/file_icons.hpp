#pragma once

#include <SDL2/SDL.h>
#include <string>
#include <unordered_map>

// Colored per-file-type icons — same curated set and mapping as the macOS
// app's FileIcon.swift (sourced from the MIT-licensed Material Icon Theme,
// pkief.material-icon-theme), rasterized to romfs:/icons/<name>.png ahead
// of time since there's no SVG renderer available on-device. Keep this
// mapping and the Mac one in sync if either changes.
namespace FileIcons {

// Icon name for a given file/folder name — "folder" or "file" (the
// generic fallbacks) if nothing more specific matches.
std::string name(const std::string& fileName, bool isDirectory);

// Loads (and caches) the SDL_Texture for an icon name. Returns nullptr if
// the icon can't be loaded — callers should fall back to the plain
// colored-rect glyph in that case, same as before this existed.
SDL_Texture* texture(SDL_Renderer* renderer, const std::string& iconName);

// Frees every cached texture — call once at shutdown.
void releaseAll();

}
