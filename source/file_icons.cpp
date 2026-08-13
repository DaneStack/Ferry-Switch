#include "file_icons.hpp"

#include <SDL2/SDL_image.h>
#include <algorithm>

namespace FileIcons {

namespace {

const std::unordered_map<std::string, std::string> extensionIcons = {
    {"7z", "zip"},
    {"aac", "audio"},
    {"ai", "adobe-illustrator"},
    {"app", "exe"},
    {"asm", "assembly"},
    {"avi", "video"},
    {"bash", "console"},
    {"bin", "hex"},
    {"bmp", "image"},
    {"bz2", "zip"},
    {"c", "c"},
    {"cc", "cpp"},
    {"cer", "certificate"},
    {"cfg", "settings"},
    {"cjs", "javascript"},
    {"class", "javaclass"},
    {"conf", "settings"},
    {"cpp", "cpp"},
    {"crt", "certificate"},
    {"cs", "csharp"},
    {"css", "css"},
    {"csv", "table"},
    {"cxx", "cpp"},
    {"dart", "dart"},
    {"db", "database"},
    {"dmg", "disc"},
    {"doc", "word"},
    {"docx", "word"},
    {"elf", "exe"},
    {"env", "tune"},
    {"exe", "exe"},
    {"flac", "audio"},
    {"flv", "video"},
    {"gif", "image"},
    {"go", "go"},
    {"gz", "zip"},
    {"h", "h"},
    {"heic", "image"},
    {"hpp", "hpp"},
    {"htm", "html"},
    {"html", "html"},
    {"ico", "image"},
    {"ini", "settings"},
    {"iso", "disc"},
    {"jar", "jar"},
    {"java", "java"},
    {"jpeg", "image"},
    {"jpg", "image"},
    {"js", "javascript"},
    {"json", "json"},
    {"jsx", "react"},
    {"key", "key"},
    {"keys", "key"},
    {"kt", "kotlin"},
    {"kts", "kotlin"},
    {"less", "less"},
    {"lock", "lock"},
    {"log", "log"},
    {"lua", "lua"},
    {"m", "objective-c"},
    {"m4a", "audio"},
    {"markdown", "markdown"},
    {"md", "markdown"},
    {"mjs", "javascript"},
    {"mkv", "video"},
    {"mm", "objective-cpp"},
    {"mov", "video"},
    {"mp3", "audio"},
    {"mp4", "video"},
    {"nca", "exe"},
    {"nfo", "document"},
    {"nro", "exe"},
    {"nsp", "exe"},
    {"nsz", "exe"},
    {"ogg", "video"},
    {"otf", "font"},
    {"pdf", "pdf"},
    {"pem", "key"},
    {"php", "php"},
    {"pkg", "exe"},
    {"pl", "prolog"},
    {"plist", "xml"},
    {"png", "image"},
    {"ppt", "powerpoint"},
    {"pptx", "powerpoint"},
    {"properties", "settings"},
    {"ps1", "powershell"},
    {"psd", "adobe-photoshop"},
    {"py", "python"},
    {"pyc", "python-misc"},
    {"r", "r"},
    {"rar", "zip"},
    {"rb", "ruby"},
    {"rs", "rust"},
    {"rtf", "word"},
    {"sass", "sass"},
    {"scss", "sass"},
    {"sh", "console"},
    {"sql", "database"},
    {"sqlite", "database"},
    {"svelte", "svelte"},
    {"svg", "svg"},
    {"swift", "swift"},
    {"tar", "zip"},
    {"tiff", "image"},
    {"toml", "toml"},
    {"ts", "typescript"},
    {"tsv", "table"},
    {"tsx", "react_ts"},
    {"ttf", "font"},
    {"txt", "document"},
    {"vb", "visualstudio"},
    {"vue", "vue"},
    {"wav", "audio"},
    {"webm", "video"},
    {"webp", "image"},
    {"woff", "font"},
    {"woff2", "font"},
    {"xci", "exe"},
    {"xcz", "exe"},
    {"xls", "table"},
    {"xlsx", "table"},
    {"xml", "xml"},
    {"xz", "zip"},
    {"yaml", "yaml"},
    {"yml", "yaml"},
    {"zip", "zip"},
    {"zsh", "console"},
};

const std::unordered_map<std::string, std::string> filenameIcons = {
    {".gitattributes", "git"},
    {".gitignore", "git"},
    {".gitmodules", "git"},
    {"dockerfile", "docker"},
    {"license", "license"},
    {"makefile", "makefile"},
    {"readme", "readme"},
};

std::unordered_map<std::string, SDL_Texture*> textureCache;

}

std::string name(const std::string& fileName, bool isDirectory) {
    if (isDirectory) {
        return "folder";
    }
    std::string lower = fileName;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    auto byName = filenameIcons.find(lower);
    if (byName != filenameIcons.end()) {
        return byName->second;
    }
    size_t dot = lower.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < lower.size()) {
        auto byExt = extensionIcons.find(lower.substr(dot + 1));
        if (byExt != extensionIcons.end()) {
            return byExt->second;
        }
    }
    return "file";
}

SDL_Texture* texture(SDL_Renderer* renderer, const std::string& iconName) {
    auto it = textureCache.find(iconName);
    if (it != textureCache.end()) {
        return it->second;
    }
    std::string path = "romfs:/icons/" + iconName + ".png";
    SDL_Texture* tex = IMG_LoadTexture(renderer, path.c_str());
    textureCache[iconName] = tex; // cache the miss too (nullptr) so we don't retry every frame
    return tex;
}

void releaseAll() {
    for (auto& [name, tex] : textureCache) {
        if (tex) {
            SDL_DestroyTexture(tex);
        }
    }
    textureCache.clear();
}

}
