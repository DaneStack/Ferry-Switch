#pragma once

#include <string>
#include <vector>

struct Entry {
    std::string name;
    bool isDir;
    long long size;
    bool isParentLink = false;
};

class FileBrowser {
public:
    explicit FileBrowser(std::string startPath);

    const std::vector<Entry>& entries() const { return m_entries; }
    const std::string& path() const { return m_path; }
    size_t selection() const { return m_selection; }

    void moveSelection(int delta);
    void setSelection(size_t index);
    void enterSelected();
    void goUp();
    void refresh();

    bool createFolder(const std::string& name);
    bool deleteSelected();

    const Entry* selectedEntry() const;

private:
    std::string m_path;
    std::vector<Entry> m_entries;
    size_t m_selection = 0;

    static bool isRoot(const std::string& path);
};
