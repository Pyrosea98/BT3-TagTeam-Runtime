#include "frontend/fe_picker.h"

#include "frontend/fe_ui.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

namespace
{
    std::string toLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    bool isDir(const std::filesystem::path &p)
    {
        std::error_code ec;
        return std::filesystem::is_directory(p, ec);
    }

    // Shorten a path for a one-line label: C:\Users\Rexx\Downloads -> C:\...\Downloads
    std::string shortPath(const std::filesystem::path &p)
    {
        const std::string s = p.string();
        if (s.size() <= 42)
            return s;
        return s.substr(0, 3) + "..." + s.substr(s.size() - 38);
    }

#if defined(_WIN32)
    // Volume label ("Windows", "DATA"), so two drives are told apart by more than their letter.
    std::string volumeLabel(const std::string &root)
    {
        char name[256] = "";
        const DWORD n = GetVolumeInformationA(root.c_str(), name, sizeof name, nullptr, nullptr,
                                              nullptr, nullptr, 0);
        if (n && name[0])
            return name;
        return std::string();
    }

    // Every drive Windows currently has mounted. The old picker started at C:\ and offered no way
    // out of it, which is exactly the case this replaces.
    void appendVolumes(std::vector<std::pair<std::string, std::filesystem::path>> &out)
    {
        DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26; ++i)
        {
            if (!(mask & (1u << i)))
                continue;
            const std::string root = std::string(1, (char)('A' + i)) + ":\\";
            const UINT type = GetDriveTypeA(root.c_str());
            if (type == DRIVE_NO_ROOT_DIR)
                continue;   // an empty card reader slot, not a volume
            std::string label = volumeLabel(root);
            if (type == DRIVE_CDROM)
                label = label.empty() ? "CD/DVD" : label;
            else if (label.empty())
                label = type == DRIVE_REMOVABLE ? "Removible" : "Unidad";
            out.push_back({root + "  " + label, std::filesystem::path(root)});
        }
    }
#elif defined(__APPLE__)
    // macOS: the root volume plus every disk under /Volumes, which is also where an attached disk
    // image shows up -- i.e. exactly where a dump usually lives.
    void appendVolumes(std::vector<std::pair<std::string, std::filesystem::path>> &out)
    {
        out.push_back({"/  Macintosh HD", std::filesystem::path("/")});
        std::error_code ec;
        for (const auto &de : std::filesystem::directory_iterator("/Volumes", ec))
        {
            if (ec)
                break;
            if (isDir(de.path()))
                out.push_back({de.path().filename().string(), de.path()});
        }
    }
#else
    // Linux/BSD: a dump is very often on a disk that is NOT under $HOME (a separate data disk, an
    // external drive, a NAS mount). This used to list nothing at all here, so the places bar held
    // only the $HOME folders and the sole way onto another disk was typing the path by hand.
    //
    // The mount table is the equivalent of what GetLogicalDrives gives on Windows: every mounted
    // filesystem, with its device and type. Kernel/pseudo filesystems are filtered out (nowhere a
    // user keeps a disc image), the mount point is shown next to a trimmed device name so two
    // identically named disks stay distinguishable, and duplicates from bind mounts are dropped.
    void appendVolumes(std::vector<std::pair<std::string, std::filesystem::path>> &out)
    {
        // "/" first: it is in the mount table too, but it is the one entry that must never be
        // missing, and a trimmed /proc/mounts still beats an empty bar.
        out.push_back({"/  raiz", std::filesystem::path("/")});

        std::ifstream mounts("/proc/mounts");
        if (!mounts)
            return;   // no procfs (a container, or a BSD): "/" above is still there

        // The root filesystem's device, so its other mount points can be skipped: on a normal
        // desktop those are bind mounts (/home, /boot, /var/log, /var/cache/pacman/pkg), and four
        // buttons for the same disk the "/" button already reaches is noise, not navigation.
        std::string rootDevice;
        {
            std::ifstream r("/proc/mounts");
            std::string line;
            while (std::getline(r, line))
            {
                std::istringstream is(line);
                std::string device, mountPoint, type;
                if ((is >> device >> mountPoint >> type) && mountPoint == "/")
                {
                    rootDevice = device;
                    break;
                }
            }
        }

        static const char *const kSkipTypes[] = {
            "proc", "sysfs", "devtmpfs", "devpts", "tmpfs", "cgroup", "cgroup2", "securityfs",
            "pstore", "bpf", "configfs", "debugfs", "tracefs", "fusectl", "mqueue", "hugetlbfs",
            "autofs", "binfmt_misc", "efivarfs", "ramfs", "squashfs", "overlay", "nfsd",
        };
        std::vector<std::string> seen;
        std::string line;
        while (std::getline(mounts, line))
        {
            std::istringstream is(line);
            std::string device, mountPoint, type;
            if (!(is >> device >> mountPoint >> type))
                continue;
            bool skip = false;
            for (const char *t : kSkipTypes)
                if (type == t) { skip = true; break; }
            if (skip || mountPoint == "/")
                continue;
            // /run, /proc, /dev and /sys hold runtime plumbing, not files a user browses for a
            // dump: the XDG document portal (/run/user/1000/doc) and a udisks2 mount both show up
            // there and neither is somewhere you keep a disc image.
            if (mountPoint.rfind("/run/", 0) == 0 || mountPoint.rfind("/proc/", 0) == 0 ||
                mountPoint.rfind("/dev/", 0) == 0 || mountPoint.rfind("/sys/", 0) == 0)
                continue;
            if (!rootDevice.empty() && device == rootDevice)
                continue;   // a bind mount of the root disk, not a volume of its own
            // /proc/mounts escapes spaces and other awkward characters as \040 and friends.
            std::string decoded;
            for (std::size_t i = 0; i < mountPoint.size(); ++i)
            {
                if (mountPoint[i] == '\\' && i + 3 < mountPoint.size())
                {
                    const std::string hex = mountPoint.substr(i + 1, 3);
                    decoded += (char)std::stoi(hex, nullptr, 16);
                    i += 3;
                }
                else
                    decoded += mountPoint[i];
            }
            const std::filesystem::path p(decoded);
            if (!isDir(p))
                continue;   // a stale or unreachable mount point
            if (std::find(seen.begin(), seen.end(), decoded) != seen.end())
                continue;   // the same mount reachable twice
            seen.push_back(decoded);

            // The device basename is the disk name the user recognises; the mount point next to it
            // keeps two disks of the same model apart.
            std::string dev = device;
            const std::size_t slash = dev.find_last_of('/');
            if (slash != std::string::npos)
                dev = dev.substr(slash + 1);
            if (dev.empty())
                dev = type;
            out.push_back({dev + "  " + decoded, p});
        }
    }
#endif
}

namespace frontend
{
    void FilePicker::open(const std::string &title, std::filesystem::path startDir,
                          std::vector<std::string> extensions)
    {
        m_open = true;
        m_accepted = false;
        m_justOpened = true;   // the modal is opened by draw(), not here: it needs a frame
        m_title = title;
        m_exts = std::move(extensions);
        m_selected.clear();
        m_result.clear();
        buildPlaces();
        goTo(startDir);
    }

    void FilePicker::buildPlaces()
    {
        m_places.clear();
        m_shortcuts.clear();
        std::vector<std::pair<std::string, std::filesystem::path>> vols;
        appendVolumes(vols);
        for (const auto &v : vols)
            m_places.push_back({v.first, v.second});

        // The user folders people actually keep dumps in, when they exist.
        std::error_code ec;
        std::filesystem::path home;
#if defined(_WIN32)
        if (const char *h = std::getenv("USERPROFILE"))
            home = h;
        else if (const char *h = std::getenv("HOME"))
            home = h;
#else
        if (const char *h = std::getenv("HOME"))
            home = h;
#endif
        if (home.empty())
            home = std::filesystem::current_path(ec);
        struct { const char *label; const char *sub; } shortcuts[] = {
            {"Descargas", "Downloads"}, {"Escritorio", "Desktop"}, {"Documentos", "Documents"},
            {"Videos", "Videos"},
        };
        for (const auto &s : shortcuts)
        {
            const std::filesystem::path p = home / s.sub;
            if (isDir(p))
                m_shortcuts.push_back({s.label, p});
        }
    }

    int FilePicker::placeCombo(const char *label, const std::vector<Place> &items)
    {
        // The preview is where we actually are when that is one of the entries, and a hint
        // otherwise: showing a fixed "pick one" while sitting inside /mnt/Datos is a worse answer
        // than naming the disk you are on.
        int current = -1;
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            std::error_code ec;
            if (std::filesystem::equivalent(m_dir, items[i].path, ec) && !ec)
            {
                current = (int)i;
                break;
            }
        }
        const std::string preview =
            current >= 0 ? shortPath(items[current].path) : std::string("(elegir)");

        ImGui::SetNextItemWidth(300.0f);
        if (!ImGui::BeginCombo(label, preview.c_str()))
            return -1;
        int picked = -1;
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const bool sel = (int)i == current;
            if (ImGui::Selectable(items[i].label.c_str(), sel))
                picked = (int)i;
            if (sel)
                ImGui::SetItemDefaultFocus();
        }
        // Inside the if: BeginCombo returning false opened no window, so there is nothing to end.
        ImGui::EndCombo();
        return picked;
    }

    void FilePicker::goTo(const std::filesystem::path &p)
    {
        std::error_code ec;
        std::filesystem::path target = p;
        if (isDir(target))
        {
            m_dir = target;
        }
        else
        {
            // A file, or something that does not exist: start from the deepest parent that does.
            std::filesystem::path parent = target.parent_path();
            if (parent.empty())
                parent = std::filesystem::path(target).root_path();
            while (!parent.empty() && !isDir(parent))
                parent = parent.parent_path();
            if (parent.empty())
                parent = std::filesystem::current_path(ec);
            m_dir = parent;
        }
        m_selected.clear();
        listDir();
    }

    void FilePicker::acceptCurrent()
    {
        if (m_selected.empty())
            return;
        m_result = m_selected.string();
        m_accepted = true;
    }

    bool FilePicker::matches(const Entry &e) const
    {
        if (e.dir)
            return true;
        if (m_exts.empty())
            return true;
        const std::string lower = toLower(e.name);
        for (const std::string &x : m_exts)
            if (lower.size() >= x.size() && lower.compare(lower.size() - x.size(), x.size(), x) == 0)
                return true;
        return false;
    }

    void FilePicker::listDir()
    {
        m_entries.clear();
        std::error_code ec;
        if (!isDir(m_dir))
        {
            m_dir = m_dir.root_path();
            if (!isDir(m_dir))
                return;
        }
        std::snprintf(m_pathBuffer, sizeof m_pathBuffer, "%s", m_dir.string().c_str());

        for (const auto &de : std::filesystem::directory_iterator(m_dir, ec))
        {
            if (ec)
                break;
            Entry e;
            e.name = de.path().filename().string();
            if (e.name.empty() || e.name == "." || e.name == "..")
                continue;
            e.dir = de.is_directory(ec);
            if (!e.dir)
            {
                const std::uintmax_t sz = de.file_size(ec);
                if (!ec)
                    e.size = (unsigned long long)sz;
            }
            if (matches(e))
                m_entries.push_back(e);
        }
        std::sort(m_entries.begin(), m_entries.end(), [](const Entry &a, const Entry &b)
        {
            if (a.dir != b.dir)
                return a.dir;
            return toLower(a.name) < toLower(b.name);
        });
    }

    bool FilePicker::draw()
    {
        if (!m_open)
            return false;

        // Open on the frame the modal is asked for, not every frame: a repeated OpenPopup resets
        // the modal's state and swallows the click that was meant to close it.
        if (m_justOpened)
        {
            m_justOpened = false;
            ImGui::OpenPopup("##fe_picker");
        }

        bool result = false;
        const bool visible = ImGui::BeginPopupModal("##fe_picker", nullptr,
                                                    ImGuiWindowFlags_AlwaysAutoResize);
        if (visible)
        {
            ImGui::TextColored(fe::gold(), "%s", m_title.c_str());
            ImGui::Separator();

            // Where to jump: every mounted volume, then the user folders. Two dropdowns rather
            // than one row of buttons, because the number of mounts is whatever the machine
            // happens to have and a row of them runs off the edge of the modal.
            if (!m_places.empty())
            {
                const int vol = placeCombo("Discos:", m_places);
                if (vol >= 0)
                    goTo(m_places[vol].path);
            }
            if (!m_shortcuts.empty())
            {
                ImGui::SameLine();
                const int sc = placeCombo("Carpetas:", m_shortcuts);
                if (sc >= 0)
                    goTo(m_shortcuts[sc].path);
            }
            ImGui::Separator();

            ImGui::SetNextItemWidth(560.0f);
            if (ImGui::InputText("##path", m_pathBuffer, sizeof m_pathBuffer))
            {
                const std::filesystem::path typed(m_pathBuffer);
                if (isDir(typed))
                    goTo(typed);
            }
            ImGui::SameLine();
            if (ImGui::Button("Ir"))
                goTo(std::filesystem::path(m_pathBuffer));
            ImGui::SameLine();
            if (ImGui::Button("Subir"))
            {
                const std::filesystem::path parent = m_dir.parent_path();
                if (!parent.empty())
                    goTo(parent);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("  %s", shortPath(m_dir).c_str());

            ImGui::Separator();
            ImGui::BeginChild("##fe_picker_list", ImVec2(620.0f, 300.0f), ImGuiChildFlags_Borders);
            {
                ImDrawList *dl = ImGui::GetWindowDrawList();
                const ImU32 dirCol = ImGui::GetColorU32(fe::gold());
                const ImU32 fileCol = ImGui::GetColorU32(fe::dbz(0.62f, 0.68f, 0.78f));
                const ImU32 dimCol = ImGui::GetColorU32(fe::dbz(0.45f, 0.48f, 0.55f));
                const float rowH = ImGui::GetTextLineHeight();
                const std::filesystem::path listedDir = m_dir;

                for (const Entry &e : m_entries)
                {
                    ImGui::PushID(e.name.c_str());
                    // The row is an empty selectable and the glyph plus the text are painted on its
                    // draw list: Russo One has no folder or page character, and a "[DIR]" prefix
                    // read like a log line rather than a file browser.
                    const bool sel = !e.dir && m_selected == (m_dir / e.name);
                    ImGui::Selectable("##row", sel);
                    const ImVec2 a = ImGui::GetItemRectMin();
                    const ImVec2 b = ImGui::GetItemRectMax();
                    const float cy = (a.y + b.y) * 0.5f;
                    const float ix = a.x + 11.0f;

                    if (e.dir)
                    {
                        // Folder: body plus a tab on the top left.
                        const ImVec2 bodyMin(a.x + 4.0f, cy - rowH * 0.34f);
                        const ImVec2 bodyMax(a.x + 19.0f, cy + rowH * 0.40f);
                        dl->AddRectFilled(bodyMin, ImVec2(a.x + 12.0f, cy - rowH * 0.44f), dirCol);
                        dl->AddRectFilled(ImVec2(a.x + 4.0f, cy - rowH * 0.34f), bodyMax, dirCol);
                        dl->AddRectFilled(ImVec2(a.x + 4.0f, cy - rowH * 0.34f),
                                          ImVec2(a.x + 19.0f, cy - rowH * 0.20f), dirCol);
                    }
                    else
                    {
                        // Page: a rectangle with the top-right corner folded over.
                        const ImVec2 p0(a.x + 6.0f, cy - rowH * 0.44f);
                        const ImVec2 p1(a.x + 17.0f, cy + rowH * 0.44f);
                        const float fold = rowH * 0.22f;
                        dl->AddRectFilled(p0, p1, fileCol);
                        dl->AddTriangleFilled(ImVec2(p1.x - fold, p0.y), ImVec2(p1.x, p0.y),
                                              ImVec2(p1.x, p0.y + fold), dimCol);
                    }

                    const char *name = e.name.c_str();
                    dl->AddText(ImVec2(ix + 14.0f, a.y + (rowH - ImGui::GetFontSize()) * 0.5f),
                                sel ? dirCol : ImGui::GetColorU32(ImGuiCol_Text), name);
                    if (!e.dir)
                    {
                        char sz[64];
                        if (e.size >= 1073741824ull)
                            std::snprintf(sz, sizeof sz, "%.2f GB", e.size / 1073741824.0);
                        else
                            std::snprintf(sz, sizeof sz, "%.1f MB", e.size / 1048576.0);
                        const float w = ImGui::CalcTextSize(sz).x;
                        dl->AddText(ImVec2(b.x - 10.0f - w, a.y + (rowH - ImGui::GetFontSize()) * 0.5f),
                                    dimCol, sz);
                    }

                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        if (e.dir)
                            goTo(m_dir / e.name);
                        else
                        {
                            m_selected = m_dir / e.name;
                            acceptCurrent();
                        }
                    }
                    else if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                    {
                        if (e.dir)
                            goTo(m_dir / e.name);   // single click walks into the folder
                        else
                            m_selected = m_dir / e.name;
                    }
                    ImGui::PopID();
                    if (m_dir != listedDir)
                        break;   // the entry list was just rebuilt; stop iterating it
                }
            }
            ImGui::EndChild();

            ImGui::Separator();
            if (m_selected.empty())
                ImGui::TextDisabled("Double-click a file to choose it.");
            else
                ImGui::TextWrapped("%s", m_selected.string().c_str());
            ImGui::TextDisabled("%zu entries%s", m_entries.size(),
                                m_exts.empty() ? "" : " (filtered)");

            ImGui::BeginDisabled(m_selected.empty());
            if (ImGui::Button("Seleccionar"))
                acceptCurrent();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                m_open = false;
            // Inside the if, like comboRowStr: a Begin* that returns false opened no window.
            // BeginPopupModal even ends the popup itself when its own Begin fails, so ending it
            // here too was a double-end.
            ImGui::EndPopup();
        }

        if (m_accepted)
        {
            m_open = false;
            result = true;
            m_accepted = false;
        }
        return result;
    }
}
