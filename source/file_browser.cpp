#include "file_browser.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace {

std::string joinPath(const std::string& base, const std::string& name) {
    if (!base.empty() && base.back() == '/') {
        return base + name;
    }
    return base + "/" + name;
}

bool removeRecursively(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        return std::remove(path.c_str()) == 0;
    }

    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return false;
    }
    bool ok = true;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        ok = removeRecursively(joinPath(path, ent->d_name)) && ok;
    }
    closedir(dir);
    return (std::remove(path.c_str()) == 0) && ok;
}

}

FileBrowser::FileBrowser(std::string startPath) : m_path(std::move(startPath)) {
    refresh();
}

bool FileBrowser::isRoot(const std::string& path) {
    return path == "sdmc:/" || path == "sdmc:";
}

void FileBrowser::refresh() {
    m_entries.clear();

    DIR* dir = opendir(m_path.c_str());
    if (!dir) {
        m_selection = 0;
        return;
    }

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        Entry entry;
        entry.name = ent->d_name;

        std::string full = joinPath(m_path, entry.name);
        struct stat st;
        if (stat(full.c_str(), &st) == 0) {
            entry.isDir = S_ISDIR(st.st_mode);
            entry.size = entry.isDir ? 0 : static_cast<long long>(st.st_size);
        } else {
            entry.isDir = (ent->d_type == DT_DIR);
            entry.size = 0;
        }
        m_entries.push_back(std::move(entry));
    }
    closedir(dir);

    std::sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
        if (a.isDir != b.isDir) {
            return a.isDir > b.isDir;
        }
        return a.name < b.name;
    });

    if (!isRoot(m_path)) {
        Entry parentLink;
        parentLink.name = "..";
        parentLink.isDir = true;
        parentLink.size = 0;
        parentLink.isParentLink = true;
        m_entries.insert(m_entries.begin(), std::move(parentLink));
    }

    if (m_selection >= m_entries.size()) {
        m_selection = m_entries.empty() ? 0 : m_entries.size() - 1;
    }
}

void FileBrowser::moveSelection(int delta) {
    if (m_entries.empty()) {
        return;
    }
    long next = static_cast<long>(m_selection) + delta;
    if (next < 0) {
        next = 0;
    }
    if (next >= static_cast<long>(m_entries.size())) {
        next = static_cast<long>(m_entries.size()) - 1;
    }
    m_selection = static_cast<size_t>(next);
}

void FileBrowser::setSelection(size_t index) {
    if (index < m_entries.size()) {
        m_selection = index;
    }
}

void FileBrowser::enterSelected() {
    const Entry* entry = selectedEntry();
    if (!entry || !entry->isDir) {
        return;
    }
    if (entry->isParentLink) {
        goUp();
        return;
    }
    m_path = joinPath(m_path, entry->name);
    m_selection = 0;
    refresh();
}

void FileBrowser::goUp() {
    if (isRoot(m_path)) {
        return;
    }
    size_t lastSlash = m_path.find_last_of('/');
    if (lastSlash == std::string::npos) {
        return;
    }
    std::string parent = m_path.substr(0, lastSlash);
    if (parent.empty() || parent == "sdmc:") {
        parent = "sdmc:/";
    }
    m_path = parent;
    m_selection = 0;
    refresh();
}

bool FileBrowser::createFolder(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    std::string full = joinPath(m_path, name);
    if (mkdir(full.c_str(), 0777) != 0) {
        return false;
    }
    refresh();
    return true;
}

bool FileBrowser::deleteSelected() {
    const Entry* entry = selectedEntry();
    if (!entry || entry->isParentLink) {
        return false;
    }
    std::string full = joinPath(m_path, entry->name);
    bool ok = removeRecursively(full);
    refresh();
    return ok;
}

const Entry* FileBrowser::selectedEntry() const {
    if (m_selection >= m_entries.size()) {
        return nullptr;
    }
    return &m_entries[m_selection];
}
