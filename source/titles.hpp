#pragma once

#include <string>
#include <vector>

#include <switch.h>

// One installed title, as needed for the "Installed Games"/"Saves" virtual
// FTP roots — just enough to build a friendly, unique folder name per
// title (see ftp_server.cpp).
struct TitleInfo {
    u64 applicationId = 0;
    std::string displayName; // "Game Name [0100ABCD00000000]" — always unique
};

// Queries `ns` for every installed application record and its NACP title
// name. Capped at a generous but bounded count, and does one blocking IPC
// round-trip per title — only meant to be called occasionally (entering
// the virtual root), not per frame; callers should cache the result for
// the rest of the session.
std::vector<TitleInfo> listInstalledTitles();
