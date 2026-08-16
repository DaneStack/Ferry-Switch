#include "titles.hpp"

#include <cstdio>
#include <memory>

namespace {

// These names are not only listing text — they become the filename of the
// generated .nsp, and the whole point of that file is to be downloaded and
// then put back on the SD card for an installer to pick up. The card is
// FAT/exFAT, so a name it rejects means a dump that cannot make the return
// trip: a title like "Animal Crossing: New Horizons" downloaded fine and
// then failed to upload with "550 Could not create file", because of the
// colon alone. So the whole set FAT refuses is flattened here, not just the
// separators that would corrupt a listing. Control characters go too —
// some homebrew forwarders install themselves under genuinely strange
// titles.
std::string sanitize(const std::string& raw) {
    std::string out = raw;
    for (char& c : out) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|' || static_cast<unsigned char>(c) < 0x20) {
            c = '_';
        }
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
        out.pop_back();
    }
    if (out.empty()) {
        out = "Unknown";
    }
    return out;
}

// A generous cap on how many installed titles to enumerate — bounds the
// worst-case time spent doing IPC round-trips (one per title) when this
// is called, and comfortably covers even a heavily-modded console.
constexpr s32 maxTitles = 512;

}

std::vector<TitleInfo> listInstalledTitles() {
    std::vector<TitleInfo> result;

    std::vector<NsApplicationRecord> records(maxTitles);
    s32 total = 0;
    if (R_FAILED(nsListApplicationRecord(records.data(), maxTitles, 0, &total))) {
        return result;
    }
    if (total > maxTitles) {
        total = maxTitles;
    }

    // NsApplicationControlData is ~144KB (mostly the JPEG icon we don't
    // use) — heap-allocated once and reused across the loop rather than
    // per title, both to avoid a huge stack frame and repeated large
    // allocations.
    auto controlData = std::make_unique<NsApplicationControlData>();

    for (s32 i = 0; i < total; i++) {
        u64 appId = records[i].application_id;
        std::string name;

        u64 actualSize = 0;
        if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, appId,
                                                      controlData.get(), sizeof(NsApplicationControlData),
                                                      &actualSize))) {
            NacpLanguageEntry* entry = nullptr;
            if (R_SUCCEEDED(nacpGetLanguageEntry(&controlData->nacp, &entry)) && entry) {
                name = entry->name;
            }
        }

        char idHex[20];
        std::snprintf(idHex, sizeof(idHex), "%016llX", static_cast<unsigned long long>(appId));
        if (name.empty()) {
            name = idHex;
        }

        TitleInfo info;
        info.applicationId = appId;
        // The hex ID suffix guarantees uniqueness (two titles can share a
        // display name) and gives anyone comparing against other tools a
        // way to cross-reference the exact title.
        info.displayName = sanitize(name) + " [" + idHex + "]";
        result.push_back(std::move(info));
    }

    return result;
}
