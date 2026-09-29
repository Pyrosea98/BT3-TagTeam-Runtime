#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace frontend
{
    // In-app file picker. SDL2 has no native open dialog (SDL_ShowOpenFileDialog is SDL3), and
    // the front-end does not carry Qt, so the browser is built from ImGui: a places bar with every
    // mounted volume, a path box, "up" navigation and a filtered listing. Portable, and it looks
    // like the rest of the HUD.
    class FilePicker
    {
    public:
        // extensions are lowercase, with the dot (".iso", ".7z"); empty means "all files".
        void open(const std::string &title, std::filesystem::path startDir,
                  std::vector<std::string> extensions);
        void close() { m_open = false; }
        bool isOpen() const { return m_open; }

        // Draws the modal. Returns true once (and only once) the user accepted a file.
        bool draw();

        const std::string &result() const { return m_result; }

    private:
        struct Entry
        {
            std::string name;
            bool dir = false;
            unsigned long long size = 0;
        };

        // A jump target in the places bar: a mounted volume, a user folder, or the start dir.
        struct Place
        {
            std::string label;
            std::filesystem::path path;
        };

        void buildPlaces();
        void listDir();
        bool matches(const Entry &e) const;
        void goTo(const std::filesystem::path &p);
        void acceptCurrent();

        // One dropdown of jump targets. Returns the index picked this frame, or -1. A dropdown and
        // not a row of buttons: a machine can have a dozen mounts and a row of them runs off the
        // edge of the modal, while the dropdown costs a fixed width no matter how many there are.
        int placeCombo(const char *label, const std::vector<Place> &items, float width);

        bool m_open = false;
        bool m_accepted = false;
        bool m_justOpened = false;
        std::string m_title;
        std::filesystem::path m_dir;
        std::filesystem::path m_selected;
        char m_pathBuffer[1024] = {};
        std::vector<std::string> m_exts;
        std::vector<Entry> m_entries;
        std::vector<Place> m_places;      // every mounted volume
        std::vector<Place> m_shortcuts;   // the user folders under $HOME
        std::string m_result;
    };
}
