#include "Common.h"
#include "runtime/ps2_statesync.h"   // [statesync]
#include "MemoryCard.h"
#include <cstdarg>

namespace ps2_stubs
{
    namespace
    {
        constexpr int32_t kMcCmdGetInfo = 0x01;
        constexpr int32_t kMcCmdOpen = 0x02;
        constexpr int32_t kMcCmdClose = 0x03;
        constexpr int32_t kMcCmdSeek = 0x04;
        constexpr int32_t kMcCmdRead = 0x05;
        constexpr int32_t kMcCmdWrite = 0x06;
        constexpr int32_t kMcCmdFlush = 0x0A;
        constexpr int32_t kMcCmdMkdir = 0x0B;
        constexpr int32_t kMcCmdChdir = 0x0C;
        constexpr int32_t kMcCmdGetDir = 0x0D;
        constexpr int32_t kMcCmdSetFileInfo = 0x0E;
        constexpr int32_t kMcCmdDelete = 0x0F;
        constexpr int32_t kMcCmdFormat = 0x10;
        constexpr int32_t kMcCmdUnformat = 0x11;
        constexpr int32_t kMcCmdGetEntSpace = 0x12;
        constexpr int32_t kMcCmdRename = 0x13;

        constexpr int32_t kMcResultSucceed = 0;
        constexpr int32_t kMcResultChangedCard = -1;
        constexpr int32_t kMcResultNoFormat = -2;
        constexpr int32_t kMcResultNoEntry = -4;
        constexpr int32_t kMcResultDeniedPermit = -5;
        constexpr int32_t kMcResultNotEmpty = -6;
        constexpr int32_t kMcResultUpLimitHandle = -7;

        constexpr int32_t kMcTypePs2 = 2;
        constexpr int32_t kMcFormatted = 1;
        constexpr int32_t kMcUnformatted = 0;
        constexpr int32_t kMcFreeClusters = 0x2000;
        constexpr size_t kMcMaxPathLen = 1024;
        constexpr size_t kMcMaxOpenFiles = 32;

        constexpr uint16_t kMcAttrReadable = 0x0001;
        constexpr uint16_t kMcAttrWriteable = 0x0002;
        constexpr uint16_t kMcAttrFile = 0x0010;
        constexpr uint16_t kMcAttrSubdir = 0x0020;
        constexpr uint16_t kMcAttrClosed = 0x0080;
        constexpr uint16_t kMcAttrExists = 0x8000;

        struct SceMcStDateTime
        {
            uint8_t Resv2 = 0;
            uint8_t Sec = 0;
            uint8_t Min = 0;
            uint8_t Hour = 0;
            uint8_t Day = 0;
            uint8_t Month = 0;
            uint16_t Year = 0;
        };

        struct SceMcTblGetDir
        {
            SceMcStDateTime _Create{};
            SceMcStDateTime _Modify{};
            uint32_t FileSizeByte = 0;
            uint16_t AttrFile = 0;
            uint16_t Reserve1 = 0;
            uint32_t Reserve2 = 0;
            uint32_t PdaAplNo = 0;
            char EntryName[32]{};
        };

        static_assert(sizeof(SceMcTblGetDir) == 64, "sceMcTblGetDir size mismatch");

        struct McOpenFile
        {
            FILE *file = nullptr;
            int32_t port = 0;
            std::filesystem::path hostPath;
        };

        struct McPortState
        {
            std::string currentDir = "/";
            bool formatted = true;
            // libmc protocol: the FIRST sceMcGetInfo after boot (or card change) must
            // return -1 ("card changed") — that result is what triggers the game's
            // save-directory scan. Returning 0 ("same card") on the first query makes
            // games keep their cached "no card" state (Continue grayed with a valid save).
            bool infoQueried = false;
        };

        std::mutex g_mcStateMutex;
        int32_t g_mcNextFd = 1;
        int32_t g_mcLastCmd = 0;
        int32_t g_mcLastResult = 0;
        std::unordered_map<int32_t, McOpenFile> g_mcFiles;
        std::array<McPortState, 2> g_mcPorts{};
        int32_t g_cvMcFileCursor = 0;
        constexpr int32_t kCvMcFreeCapacityBytes = 0x01000000;
        constexpr int32_t kCvMcSaveCapacityBytes = 0x00080000;
        constexpr int32_t kCvMcConfigCapacityBytes = 0x00008000;
        constexpr int32_t kCvMcIconCapacityBytes = 0x00004000;

        bool isValidMcPortSlot(int32_t port, int32_t slot)
        {
            return port >= 0 && port < static_cast<int32_t>(g_mcPorts.size()) && slot == 0;
        }

        std::filesystem::path getMcRootPath(int32_t port)
        {
            const PS2Runtime::IoPaths &paths = PS2Runtime::getIoPaths();
            std::filesystem::path root = paths.mcRoot;
            if (root.empty())
            {
                if (!paths.elfDirectory.empty())
                {
                    root = paths.elfDirectory / "mc0";
                }
                else
                {
                    std::error_code ec;
                    const std::filesystem::path cwd = std::filesystem::current_path(ec);
                    root = ec ? std::filesystem::path("mc0") : (cwd / "mc0");
                }
            }

            root = root.lexically_normal();
            if (port <= 0)
            {
                return root;
            }

            const std::filesystem::path parent = root.parent_path();
            const std::string leaf = root.filename().string();
            const std::string lowerLeaf = toLowerAscii(leaf);
            if (lowerLeaf == "mc0")
            {
                return (parent / "mc1").lexically_normal();
            }
            if (leaf.empty())
            {
                return (root / "mc1").lexically_normal();
            }

            return (parent / (leaf + "_slot" + std::to_string(port))).lexically_normal();
        }

        void ensureMcRootExists(int32_t port)
        {
            std::error_code ec;
            std::filesystem::create_directories(getMcRootPath(port), ec);
        }

        std::vector<std::string> splitMcPathComponents(const std::string &value)
        {
            std::vector<std::string> parts;
            std::string current;
            for (char c : value)
            {
                if (c == '/' || c == '\\')
                {
                    if (!current.empty())
                    {
                        parts.push_back(current);
                        current.clear();
                    }
                }
                else
                {
                    current.push_back(c);
                }
            }

            if (!current.empty())
            {
                parts.push_back(current);
            }

            return parts;
        }

        std::string joinMcPathComponents(const std::vector<std::string> &parts)
        {
            if (parts.empty())
            {
                return "/";
            }

            std::string joined = "/";
            for (size_t i = 0; i < parts.size(); ++i)
            {
                if (i != 0u)
                {
                    joined.push_back('/');
                }
                joined.append(parts[i]);
            }
            return joined;
        }

        std::string normalizeGuestMcPathLocked(int32_t port, std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            const std::string lower = toLowerAscii(path);
            if (lower.rfind("mc0:", 0) == 0 || lower.rfind("mc1:", 0) == 0)
            {
                path = path.substr(4);
            }

            const bool absolute = !path.empty() && path.front() == '/';
            std::vector<std::string> parts;
            if (!absolute && port >= 0 && port < static_cast<int32_t>(g_mcPorts.size()))
            {
                parts = splitMcPathComponents(g_mcPorts[static_cast<size_t>(port)].currentDir);
            }

            for (const std::string &part : splitMcPathComponents(path))
            {
                if (part.empty() || part == ".")
                {
                    continue;
                }

                if (part == "..")
                {
                    if (!parts.empty())
                    {
                        parts.pop_back();
                    }
                    continue;
                }

                parts.push_back(part);
            }

            return joinMcPathComponents(parts);
        }

        std::filesystem::path guestMcPathToHostPath(int32_t port, const std::string &guestPath)
        {
            std::filesystem::path resolved = getMcRootPath(port);
            if (guestPath.size() > 1u)
            {
                resolved /= std::filesystem::path(guestPath.substr(1));
            }
            return resolved.lexically_normal();
        }

        bool localtimeSafeMc(const std::time_t *value, std::tm *out)
        {
#ifdef _WIN32
            return localtime_s(out, value) == 0;
#else
            return localtime_r(value, out) != nullptr;
#endif
        }

        std::time_t fileTimeToTimeTMc(std::filesystem::file_time_type value)
        {
            const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                value - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
            return std::chrono::system_clock::to_time_t(systemTime);
        }

        void writeMcCString(uint8_t *rdram, uint32_t addr, const std::string &value)
        {
            if (addr == 0u)
            {
                return;
            }

            uint8_t *dst = getMemPtr(rdram, addr);
            if (!dst)
            {
                return;
            }

            std::memcpy(dst, value.c_str(), value.size() + 1u);
        }

        void writeMcDateTime(SceMcStDateTime &out, std::time_t value)
        {
            std::tm tm{};
            if (!localtimeSafeMc(&value, &tm))
            {
                std::memset(&out, 0, sizeof(out));
                return;
            }

            out.Resv2 = 0;
            out.Sec = static_cast<uint8_t>(tm.tm_sec);
            out.Min = static_cast<uint8_t>(tm.tm_min);
            out.Hour = static_cast<uint8_t>(tm.tm_hour);
            out.Day = static_cast<uint8_t>(tm.tm_mday);
            out.Month = static_cast<uint8_t>(tm.tm_mon + 1);
            out.Year = static_cast<uint16_t>(tm.tm_year + 1900);
        }

        void fillMcDirTableEntry(SceMcTblGetDir &entry,
                                 const std::string &name,
                                 bool isDirectory,
                                 uint32_t sizeBytes,
                                 std::time_t modifiedTime)
        {
            std::memset(&entry, 0, sizeof(entry));
            writeMcDateTime(entry._Create, modifiedTime);
            writeMcDateTime(entry._Modify, modifiedTime);
            entry.FileSizeByte = isDirectory ? 0u : sizeBytes;
            entry.AttrFile = static_cast<uint16_t>(kMcAttrReadable |
                                                   kMcAttrWriteable |
                                                   (isDirectory ? kMcAttrSubdir : kMcAttrFile) |
                                                   kMcAttrClosed |
                                                   kMcAttrExists);
            std::strncpy(entry.EntryName, name.c_str(), sizeof(entry.EntryName) - 1u);
            entry.EntryName[sizeof(entry.EntryName) - 1u] = '\0';
        }

        bool wildcardMatch(const std::string &pattern, const std::string &value)
        {
            size_t patternPos = 0u;
            size_t valuePos = 0u;
            size_t starPos = std::string::npos;
            size_t matchPos = 0u;

            while (valuePos < value.size())
            {
                if (patternPos < pattern.size() &&
                    (pattern[patternPos] == '?' || pattern[patternPos] == value[valuePos]))
                {
                    ++patternPos;
                    ++valuePos;
                }
                else if (patternPos < pattern.size() && pattern[patternPos] == '*')
                {
                    starPos = patternPos++;
                    matchPos = valuePos;
                }
                else if (starPos != std::string::npos)
                {
                    patternPos = starPos + 1u;
                    valuePos = ++matchPos;
                }
                else
                {
                    return false;
                }
            }

            while (patternPos < pattern.size() && pattern[patternPos] == '*')
            {
                ++patternPos;
            }

            return patternPos == pattern.size();
        }

        void setMcCommandResultLocked(int32_t cmd, int32_t result)
        {
            g_mcLastCmd = cmd;
            g_mcLastResult = result;
        }

        void closeMcFilesLocked()
        {
            for (auto &[fd, openFile] : g_mcFiles)
            {
                if (openFile.file)
                {
                    std::fclose(openFile.file);
                    openFile.file = nullptr;
                }
            }
            g_mcFiles.clear();
        }

        void closeMcFilesForPortLocked(int32_t port)
        {
            for (auto it = g_mcFiles.begin(); it != g_mcFiles.end();)
            {
                if (it->second.port == port)
                {
                    if (it->second.file)
                    {
                        std::fclose(it->second.file);
                    }
                    it = g_mcFiles.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        int32_t allocateMcFdLocked(FILE *file, int32_t port, const std::filesystem::path &hostPath)
        {
            if (!file)
            {
                return kMcResultDeniedPermit;
            }
            if (g_mcFiles.size() >= kMcMaxOpenFiles)
            {
                return kMcResultUpLimitHandle;
            }

            for (int attempt = 0; attempt < 0x10000; ++attempt)
            {
                if (g_mcNextFd <= 0)
                {
                    g_mcNextFd = 1;
                }

                const int32_t fd = g_mcNextFd++;
                if (g_mcFiles.find(fd) != g_mcFiles.end())
                {
                    continue;
                }

                g_mcFiles.emplace(fd, McOpenFile{file, port, hostPath});
                return fd;
            }

            return kMcResultUpLimitHandle;
        }

        FILE *openMcHostFile(const std::filesystem::path &hostPath, uint32_t flags)
        {
            const uint32_t access = flags & PS2_FIO_O_RDWR;
            const bool read = (access == PS2_FIO_O_RDONLY) || (access == PS2_FIO_O_RDWR);
            const bool write = (access == PS2_FIO_O_WRONLY) || (access == PS2_FIO_O_RDWR);
            const bool append = (flags & PS2_FIO_O_APPEND) != 0u;
            const bool create = (flags & PS2_FIO_O_CREAT) != 0u;
            const bool truncate = (flags & PS2_FIO_O_TRUNC) != 0u;

            std::error_code ec;
            const bool exists = std::filesystem::exists(hostPath, ec) && !ec;

            const char *mode = "rb";
            if (read && write)
            {
                if (append)
                {
                    mode = exists ? "a+b" : "w+b";
                }
                else if (truncate || (create && !exists))
                {
                    mode = "w+b";
                }
                else
                {
                    mode = "r+b";
                }
            }
            else if (write)
            {
                if (append)
                {
                    mode = exists ? "ab" : "wb";
                }
                else if (truncate || (create && !exists))
                {
                    mode = "wb";
                }
                else
                {
                    mode = "r+b";
                }
            }

            return std::fopen(hostPath.string().c_str(), mode);
        }
    }


    MemoryCardDebugSnapshot getMemoryCardDebugSnapshot()
    {
        MemoryCardDebugSnapshot snapshot{};
        std::lock_guard<std::mutex> lock(g_mcStateMutex);
        snapshot.nextFd = g_mcNextFd;
        snapshot.lastCmd = g_mcLastCmd;
        snapshot.lastResult = g_mcLastResult;
        snapshot.cvFileCursor = g_cvMcFileCursor;

        for (size_t i = 0; i < g_mcPorts.size(); ++i)
        {
            snapshot.ports[i].port = static_cast<int32_t>(i);
            snapshot.ports[i].currentDir = g_mcPorts[i].currentDir;
            snapshot.ports[i].formatted = g_mcPorts[i].formatted;
            snapshot.ports[i].rootPath = getMcRootPath(static_cast<int32_t>(i));
        }

        snapshot.openFiles.reserve(g_mcFiles.size());
        for (const auto &[fd, file] : g_mcFiles)
        {
            MemoryCardDebugOpenFile row{};
            row.fd = fd;
            row.port = file.port;
            row.hostPath = file.hostPath;
            snapshot.openFiles.push_back(std::move(row));
        }
        std::sort(snapshot.openFiles.begin(), snapshot.openFiles.end(), [](const MemoryCardDebugOpenFile &a, const MemoryCardDebugOpenFile &b)
        {
            return a.fd < b.fd;
        });
        return snapshot;
    }

    static void mcTrace(const char *fmt, ...)
    {
        // [mclog] PS2X_MCLOG=1 traces the whole save conversation, not just GetDir:
        // the freeze we are chasing could be a stall in Open/Read/GetInfo/Write, so
        // every entry point logs. The cap is per-process and was raised from 300
        // because the save-load conversation alone exceeds that; PS2X_MCLOGMAX
        // overrides it (0 = unlimited).
        static const bool s_ml = [](){ const char *v = std::getenv("PS2X_MCLOG"); return v && v[0] && v[0] != 0; }();
        static const int s_max = [](){
            const char *m = std::getenv("PS2X_MCLOGMAX");
            if (m && m[0] && m[0] != 0) return std::atoi(m);
            return 4000;
        }();
        static int s_n = 0;
        if (!s_ml) return;
        if (s_max > 0 && s_n >= s_max) return;
        ++s_n;
        va_list ap; va_start(ap, fmt);
        std::fprintf(stderr, "[mclog] ");
        std::vfprintf(stderr, fmt, ap);
        std::fprintf(stderr, "\n");
        va_end(ap);
        std::fflush(stderr);
    }

    void sceMcChangeThreadPriority(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceMcChdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
        const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
        const uint32_t pathAddr = getRegU32(ctx, 6);
        const uint32_t currentDirAddr = getRegU32(ctx, 7);
        const std::string requestedDir =
            (pathAddr != 0u) ? readPs2CStringBounded(rdram, pathAddr, kMcMaxPathLen) : std::string{};

        std::string currentDir = "/";
        int32_t result = kMcResultNoEntry;
        {
            std::lock_guard<std::mutex> lock(g_mcStateMutex);
            if (isValidMcPortSlot(port, slot))
            {
                McPortState &state = g_mcPorts[static_cast<size_t>(port)];
                currentDir = state.currentDir;
                if (!state.formatted)
                {
                    result = kMcResultNoFormat;
                }
                else
                {
                    ensureMcRootExists(port);
                    const std::string resolvedDir =
                        requestedDir.empty() ? state.currentDir : normalizeGuestMcPathLocked(port, requestedDir);
                    const std::filesystem::path hostDir = guestMcPathToHostPath(port, resolvedDir);
                    std::error_code ec;
                    if (std::filesystem::exists(hostDir, ec) && !ec &&
                        std::filesystem::is_directory(hostDir, ec))
                    {
                        state.currentDir = resolvedDir;
                        currentDir = resolvedDir;
                        result = kMcResultSucceed;
                    }
                }
            }

            setMcCommandResultLocked(kMcCmdChdir, result);
        }

        writeMcCString(rdram, currentDirAddr, currentDir);
        setReturnS32(ctx, 0);
    }

    void sceMcClose(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int32_t fd = static_cast<int32_t>(getRegU32(ctx, 4));
        int32_t result = kMcResultNoEntry;
        {
            std::lock_guard<std::mutex> lock(g_mcStateMutex);
            auto it = g_mcFiles.find(fd);
            if (it != g_mcFiles.end())
            {
                if (!it->second.file || std::fclose(it->second.file) == 0)
                {
                    result = kMcResultSucceed;
                }
                g_mcFiles.erase(it);
            }
            setMcCommandResultLocked(kMcCmdClose, result);
        }
        setReturnS32(ctx, 0);
    }

    void sceMcDelete(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
        const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
        const std::string path = readPs2CStringBounded(rdram, getRegU32(ctx, 6), kMcMaxPathLen);

        int32_t result = kMcResultNoEntry;
        {
            std::lock_guard<std::mutex> lock(g_mcStateMutex);
            if (isValidMcPortSlot(port, slot))
            {
                McPortState &state = g_mcPorts[static_cast<size_t>(port)];
                if (!state.formatted)
                {
                    result = kMcResultNoFormat;
                }
                else
                {
                    const std::string guestPath = normalizeGuestMcPathLocked(port, path);
                    if (guestPath != "/")
                    {
                        const std::filesystem::path hostPath = guestMcPathToHostPath(port, guestPath);
                        std::error_code ec;
                        if (std::filesystem::exists(hostPath, ec) && !ec)
                        {
                            if (std::filesystem::is_directory(hostPath, ec) &&
                                !std::filesystem::is_empty(hostPath, ec))
                            {
                                result = kMcResultNotEmpty;
                            }
                            else if (std::filesystem::remove(hostPath, ec) && !ec)
                            {
                                result = kMcResultSucceed;
                            }
                            else
                            {
                                result = kMcResultDeniedPermit;
                            }
                        }
                    }
                }
            }

            setMcCommandResultLocked(kMcCmdDelete, result);
        }
        setReturnS32(ctx, 0);
    }

    void sceMcEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        {
            std::lock_guard<std::mutex> lock(g_mcStateMutex);
            closeMcFilesLocked();
            g_mcNextFd = 1;
            g_mcLastCmd = 0;
            g_mcLastResult = 0;
            for (McPortState &state : g_mcPorts)
            {
                state.currentDir = "/";
            }
        }

        // libmc teardown is just a local state reset in this immediate runtime model.
        setReturnS32(ctx, 0);
    }

    void sceMcFlush(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int32_t fd = static_cast<int32_t>(getRegU32(ctx, 4));
        int32_t result = kMcResultNoEntry;
        {
            std::lock_guard<std::mutex> lock(g_mcStateMutex);
            auto it = g_mcFiles.find(fd);
            if (it != g_mcFiles.end() && it->second.file)
            {
                result = (std::fflush(it->second.file) == 0) ? kMcResultSucceed : kMcResultDeniedPermit;
            }
            setMcCommandResultLocked(kMcCmdFlush, result);
        }
        setReturnS32(ctx, 0);
    }

    void sceMcFormat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
        const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
        mcTrace("Format port=%d slot=%d  <-- BORRA la card", port, slot);

        int32_t result = kMcResultNoEntry;
        {
            std::lock_guard<std::mutex> lock(g_mcStateMutex);
            if (isValidMcPortSlot(port, slot))
            {
                closeMcFilesForPortLocked(port);
                const std::filesystem::path root = getMcRootPath(port);
                std::error_code ec;
                std::filesystem::remove_all(root, ec);
                ec.clear();
                std::filesystem::create_directories(root, ec);
                if (!ec)
                {
                    McPortState &state = g_mcPorts[static_cast<size_t>(port)];
                    state.currentDir = "/";
                    state.formatted = true;
                    result = kMcResultSucceed;
                }
            }

            setMcCommandResultLocked(kMcCmdFormat, result);
        }
        setReturnS32(ctx, 0);
    }

    void sceMcGetDir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
        const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
        const std::string rawPath = readPs2CStringBounded(rdram, getRegU32(ctx, 6), kMcMaxPathLen);
        // EE ABI passes args 5-8 in $t0-$t3 (registers 8-11), NOT on the stack.
        const int32_t maxEntries = static_cast<int32_t>(getRegU32(ctx, 8));
        const uint32_t tableAddr = getRegU32(ctx, 9);
        mcTrace("GetDir ENTER port=%d slot=%d path='%s' maxent=%d table=0x%x", port, slot, rawPath.c_str(), maxEntries, tableAddr);

        std::vector<SceMcTblGetDir> entries;
        int32_t result = kMcResultNoEntry;
        {
            std::lock_guard<std::mutex> lock(g_mcStateMutex);
            if (isValidMcPortSlot(port, slot))
            {
                McPortState &state = g_mcPorts[static_cast<size_t>(port)];
                if (!state.formatted)
                {
                    result = kMcResultNoFormat;
                }
                else
                {
                    ensureMcRootExists(port);
                    const std::string guestQuery =
                        normalizeGuestMcPathLocked(port, rawPath.empty() ? "." : rawPath);
                    const bool hasWildcard =
                        guestQuery.find('*') != std::string::npos || guestQuery.find('?') != std::string::npos;

                    const std::filesystem::path queryRel =
                        (guestQuery.size() > 1u) ? std::filesystem::path(guestQuery.substr(1)) : std::filesystem::path{};

                    std::filesystem::path parentRel;
                    std::string pattern;
                    if (hasWildcard)
                    {
                        parentRel = queryRel.parent_path();
                        pattern = queryRel.filename().string();
                    }
                    else
                    {
                        const std::filesystem::path queryHostPath = guestMcPathToHostPath(port, guestQuery);
                        std::error_code queryEc;
                        if (std::filesystem::exists(queryHostPath, queryEc) && !queryEc &&
                            std::filesystem::is_directory(queryHostPath, queryEc))
                        {
                            parentRel = queryRel;
                            pattern = "*";
                        }
                        else
                        {
                            parentRel = queryRel.parent_path();
                            pattern = queryRel.filename().string();
                        }
                    }

                    if (pattern.empty())
                    {
                        pattern = "*";
                    }

                    std::filesystem::path hostDir = getMcRootPath(port);
                    if (!parentRel.empty())
                    {
                        hostDir /= parentRel;
                    }
                    hostDir = hostDir.lexically_normal();

                    std::error_code ec;
                    if (std::filesystem::exists(hostDir, ec) && !ec &&
                        std::filesystem::is_directory(hostDir, ec))
                    {
                        const std::time_t now = std::time(nullptr);
                        auto appendSpecial = [&](const std::string &name)
                        {
                            if (!wildcardMatch(pattern, name))
                            {
                                return;
                            }
                            SceMcTblGetDir entry{};
                            fillMcDirTableEntry(entry, name, true, 0u, now);
                            entries.push_back(entry);
                        };

                        appendSpecial(".");
                        appendSpecial("..");

                        std::vector<std::filesystem::directory_entry> dirEntries;
                        for (const auto &entry : std::filesystem::directory_iterator(
                                 hostDir, std::filesystem::directory_options::skip_permission_denied, ec))
                        {
                            if (ec)
                            {
                                break;
                            }
                            dirEntries.push_back(entry);
                        }

                        std::sort(dirEntries.begin(), dirEntries.end(),
                                  [](const std::filesystem::directory_entry &lhs,
                                     const std::filesystem::directory_entry &rhs)
                                  {
                                      return toLowerAscii(lhs.path().filename().string()) <
                                             toLowerAscii(rhs.path().filename().string());
                                  });

                        for (const auto &entry : dirEntries)
                        {
                            const std::string name = entry.path().filename().string();
                            if (!wildcardMatch(pattern, name))
                            {
                                continue;
                            }

                            std::error_code entryEc;
                            const bool isDirectory = entry.is_directory(entryEc) && !entryEc;
                            const uint32_t sizeBytes =
                                isDirectory ? 0u : static_cast<uint32_t>(entry.file_size(entryEc));
                            entryEc.clear();
                            const std::time_t modifiedTime = fileTimeToTimeTMc(entry.last_write_time(entryEc));
                            SceMcTblGetDir tableEntry{};
                            fillMcDirTableEntry(tableEntry,
                                                name,
                                                isDirectory,
                                                sizeBytes,
                                                entryEc ? now : modifiedTime);
                            entries.push_back(tableEntry);
                        }

                        const size_t entryCount =
                            std::min(entries.size(), maxEntries > 0 ? static_cast<size_t>(maxEntries) : 0u);
                        if (entryCount == 0u || tableAddr == 0u)
                        {
                            result = static_cast<int32_t>(entryCount);
                        }
                        else if (uint8_t *dst = getMemPtr(rdram, tableAddr))
                        {
                            std::memcpy(dst, entries.data(), entryCount * sizeof(SceMcTblGetDir));
                            result = static_cast<int32_t>(entryCount);
                        }
                        else
                        {
                            result = kMcResultDeniedPermit;
                        }
                    }
                }
            }

            setMcCommandResultLocked(kMcCmdGetDir, result);
        }
        // [mclog] The EXIT line matters as much as the ENTER one: if the game hangs
        // while loading a real save, the last ENTER without a matching EXIT names the
        // call that never came back.
        mcTrace("GetDir EXIT  -> result=%d entries=%zu", result, entries.size());
        for (size_t e = 0; e < entries.size() && e < 4; ++e)
            mcTrace("  entry[%zu]='%s' attr=0x%x size=%u", e,
                    reinterpret_cast<const char *>(entries[e].EntryName),
                    entries[e].AttrFile, entries[e].FileSizeByte);
        setReturnS32(ctx, 0);
    }

    void sceMcGetEntSpace(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1024);
    }

    void sceMcGetInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
        const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
        const uint32_t typePtr = getRegU32(ctx, 6);
        const uint32_t freePtr = getRegU32(ctx, 7);
        const uint32_t formatPtr = getRegU32(ctx, 8); // arg 5 in $t0 (EE ABI, not stack)
        mcTrace("GetInfo port=%d slot=%d", port, slot);

        int32_t cardType = 0;
        int32_t freeBlocks = 0;
        int32_t format = kMcUnformatted;
        int32_t result = kMcResultNoEntry;

        {
            std::lock_guard<std::mutex> lock(g_mcStateMutex);
            static const bool s_forceMc = [](){ const char *v=std::getenv("PS2X_FORCE_MC"); return v&&v[0]&&v[0]!='0'; }();
            if (s_forceMc && (port == 0 || port == 1))
            {
                // TEST: pretend a formatted PS2 card is present in this port.
                cardType = kMcTypePs2;
                freeBlocks = kMcFreeClusters;
                format = kMcFormatted;
                McPortState &state = g_mcPorts[static_cast<size_t>(port == 1 ? 1 : 0)];
                result = state.infoQueried ? kMcResultSucceed : kMcResultChangedCard;
                state.infoQueried = true;
            }
            else if (isValidMcPortSlot(port, slot))
            {
                McPortState &state = g_mcPorts[static_cast<size_t>(port)];
                cardType = kMcTypePs2;
                freeBlocks = state.formatted ? kMcFreeClusters : 0;
                format = state.formatted ? kMcFormatted : kMcUnformatted;
                if (!state.formatted)
                    result = kMcResultNoFormat;
                else
                    result = state.infoQueried ? kMcResultSucceed : kMcResultChangedCard;
                state.infoQueried = true;
            }

            setMcCommandResultLocked(kMcCmdGetInfo, result);
        }

        if (typePtr != 0u)
        {
            if (uint8_t *out = getMemPtr(rdram, typePtr))
            {
                std::memcpy(out, &cardType, sizeof(cardType));
            }
        }
        if (freePtr != 0u)
        {
            if (uint8_t *out = getMemPtr(rdram, freePtr))
            {
                std::memcpy(out, &freeBlocks, sizeof(freeBlocks));
            }
        }
        if (formatPtr != 0u)
        {
            if (uint8_t *out = getMemPtr(rdram, formatPtr))
            {
                std::memcpy(out, &format, sizeof(format));
            }
        }

        mcTrace("GetInfo port=%d slot=%d -> type=%d free=%d format=%d result=%d", port, slot, cardType, freeBlocks, format, result);
        setReturnS32(ctx, 0);
    }

    void sceMcGetSlotMax(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void sceMcInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        {
            std::lock_guard<std::mutex> lock(g_mcStateMutex);
            closeMcFilesLocked();
            g_mcNextFd = 1;
            g_mcLastCmd = 0;
            g_mcLastResult = 0;
            for (McPortState &state : g_mcPorts)
            {
                state.currentDir = "/";
                state.formatted = true;
            }
        }
        ensureMcRootExists(0);
        ensureMcRootExists(1);
        setReturnS32(ctx, 0);
    }

    void sceMcMkdir(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
        const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
        const std::string path = readPs2CStringBounded(rdram, getRegU32(ctx, 6), kMcMaxPathLen);

        int32_t result = kMcResultNoEntry;
        {
            std::lock_guard<std::mutex> lock(g_mcStateMutex);
            if (isValidMcPortSlot(port, slot))
            {
                McPortState &state = g_mcPorts[static_cast<size_t>(port)];
                if (!state.formatted)
                {
                    result = kMcResultNoFormat;
                }
                else
                {
                    ensureMcRootExists(port);
                    const std::string guestPath = normalizeGuestMcPathLocked(port, path);
                    const std::filesystem::path hostPath = guestMcPathToHostPath(port, guestPath);
                    std::error_code ec;
                    if (std::filesystem::exists(hostPath, ec) && !ec)
                    {
                        result = std::filesystem::is_directory(hostPath, ec) ? kMcResultSucceed : kMcResultDeniedPermit;
                    }
                    else if (std::filesystem::create_directory(hostPath, ec) && !ec)
                    {
                        result = kMcResultSucceed;
                    }
                    else
                    {
                        result = std::filesystem::exists(hostPath.parent_path(), ec) && !ec
                                     ? kMcResultDeniedPermit
                                     : kMcResultNoEntry;
                    }
                }
            }

            setMcCommandResultLocked(kMcCmdMkdir, result);
        }
        setReturnS32(ctx, 0);
    }

    void sceMcOpen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
        const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
        const std::string path = readPs2CStringBounded(rdram, getRegU32(ctx, 6), kMcMaxPathLen);
        const uint32_t flags = getRegU32(ctx, 7);
        mcTrace("Open port=%d slot=%d path='%s' flags=0x%x", port, slot, path.c_str(), flags);

        int32_t result = kMcResultNoEntry;
        {
            std::lock_guard<std::mutex> lock(g_mcStateMutex);
            if (isValidMcPortSlot(port, slot))
            {
                McPortState &state = g_mcPorts[static_cast<size_t>(port)];
                if (!state.formatted)
                {
                    result = kMcResultNoFormat;
                }
                else
                {
                    const std::string guestPath = normalizeGuestMcPathLocked(port, path);
                    const std::filesystem::path hostPath = guestMcPathToHostPath(port, guestPath);
                    std::error_code ec;
                    const bool create = (flags & PS2_FIO_O_CREAT) != 0u;
                    const bool exists = std::filesystem::exists(hostPath, ec) && !ec;
                    if (guestPath == "/")
                    {
                        result = kMcResultDeniedPermit;
                    }
                    else if (exists && std::filesystem::is_directory(hostPath, ec))
                    {
                        result = kMcResultDeniedPermit;
                    }
                    else if (!exists && !create)
                    {
                        result = kMcResultNoEntry;
                    }
                    else if (!std::filesystem::exists(hostPath.parent_path(), ec) || ec)
                    {
                        result = kMcResultNoEntry;
                    }
                    else
                    {
                        FILE *file = openMcHostFile(hostPath, flags);
                        if (!file)
                        {
                            result = exists ? kMcResultDeniedPermit : kMcResultNoEntry;
                        }
                        else
                        {
                            result = allocateMcFdLocked(file, port, hostPath);
                            if (result < 0)
                            {
                                std::fclose(file);
                            }
                        }
                    }
                }
            }
            setMcCommandResultLocked(kMcCmdOpen, result);
        }
        setReturnS32(ctx, 0);
    }

    void sceMcRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int32_t fd = static_cast<int32_t>(getRegU32(ctx, 4));
        const uint32_t dstAddr = getRegU32(ctx, 5);
        const int32_t size = static_cast<int32_t>(getRegU32(ctx, 6));
        uint8_t *dst = (size > 0) ? getMemPtr(rdram, dstAddr) : nullptr;
        mcTrace("Read fd=%d dst=0x%x size=%d (dst=%p)", fd, dstAddr, size, (void *)dst);

        int32_t result = kMcResultNoEntry;
        {
            std::lock_guard<std::mutex> lock(g_mcStateMutex);
            auto it = g_mcFiles.find(fd);
            if (size <= 0)
            {
                result = 0;
            }
            else if (it == g_mcFiles.end() || !it->second.file)
            {
                result = kMcResultNoEntry;
            }
            else if (!dst)
            {
                result = kMcResultDeniedPermit;
            }
            else
            {
                const size_t bytesRead = std::fread(dst, 1u, static_cast<size_t>(size), it->second.file);
                result = std::ferror(it->second.file) ? kMcResultDeniedPermit : static_cast<int32_t>(bytesRead);
                if (std::ferror(it->second.file))
                {
                    std::clearerr(it->second.file);
                }
            }

            setMcCommandResultLocked(kMcCmdRead, result);
        }
        setReturnS32(ctx, 0);
    }

    void sceMcRename(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
        const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
        const std::string oldPath = readPs2CStringBounded(rdram, getRegU32(ctx, 6), kMcMaxPathLen);
        const std::string newPath = readPs2CStringBounded(rdram, getRegU32(ctx, 7), kMcMaxPathLen);

        int32_t result = kMcResultNoEntry;
        {
            std::lock_guard<std::mutex> lock(g_mcStateMutex);
            if (isValidMcPortSlot(port, slot))
            {
                McPortState &state = g_mcPorts[static_cast<size_t>(port)];
                if (!state.formatted)
                {
                    result = kMcResultNoFormat;
                }
                else
                {
                    const std::filesystem::path oldHostPath =
                        guestMcPathToHostPath(port, normalizeGuestMcPathLocked(port, oldPath));
                    const std::filesystem::path newHostPath =
                        guestMcPathToHostPath(port, normalizeGuestMcPathLocked(port, newPath));
                    std::error_code ec;
                    if (std::filesystem::exists(oldHostPath, ec) && !ec &&
                        std::filesystem::exists(newHostPath.parent_path(), ec) && !ec)
                    {
                        std::filesystem::rename(oldHostPath, newHostPath, ec);
                        result = ec ? kMcResultDeniedPermit : kMcResultSucceed;
                    }
                }
            }

            setMcCommandResultLocked(kMcCmdRename, result);
        }
        setReturnS32(ctx, 0);
    }

    void sceMcSeek(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int32_t fd = static_cast<int32_t>(getRegU32(ctx, 4));
        const int32_t offset = static_cast<int32_t>(getRegU32(ctx, 5));
        const int32_t origin = static_cast<int32_t>(getRegU32(ctx, 6));

        int32_t result = kMcResultNoEntry;
        {
            std::lock_guard<std::mutex> lock(g_mcStateMutex);
            auto it = g_mcFiles.find(fd);
            if (it != g_mcFiles.end() && it->second.file)
            {
                int whence = SEEK_SET;
                if (origin == PS2_FIO_SEEK_CUR)
                {
                    whence = SEEK_CUR;
                }
                else if (origin == PS2_FIO_SEEK_END)
                {
                    whence = SEEK_END;
                }

                if (std::fseek(it->second.file, offset, whence) == 0)
                {
                    const long position = std::ftell(it->second.file);
                    result = (position >= 0) ? static_cast<int32_t>(position) : kMcResultDeniedPermit;
                }
                else
                {
                    result = kMcResultDeniedPermit;
                }
            }

            setMcCommandResultLocked(kMcCmdSeek, result);
        }
        setReturnS32(ctx, 0);
    }

    void sceMcSetFileInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
        const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
        const std::string path = readPs2CStringBounded(rdram, getRegU32(ctx, 6), kMcMaxPathLen);

        int32_t result = kMcResultNoEntry;
        {
            std::lock_guard<std::mutex> lock(g_mcStateMutex);
            if (isValidMcPortSlot(port, slot))
            {
                McPortState &state = g_mcPorts[static_cast<size_t>(port)];
                if (!state.formatted)
                {
                    result = kMcResultNoFormat;
                }
                else
                {
                    const std::filesystem::path hostPath =
                        guestMcPathToHostPath(port, normalizeGuestMcPathLocked(port, path));
                    std::error_code ec;
                    if (std::filesystem::exists(hostPath, ec) && !ec)
                    {
                        result = kMcResultSucceed;
                    }
                }
            }

            setMcCommandResultLocked(kMcCmdSetFileInfo, result);
        }
        setReturnS32(ctx, 0);
    }

    void sceMcSync(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t cmdPtr = getRegU32(ctx, 5);
        const uint32_t resultPtr = getRegU32(ctx, 6);
        int32_t cmd = 0;
        int32_t result = 0;
        {
            std::lock_guard<std::mutex> lock(g_mcStateMutex);
            cmd = g_mcLastCmd;
            result = g_mcLastResult;
        }

        if (cmdPtr != 0u)
        {
            if (uint8_t *out = getMemPtr(rdram, cmdPtr))
            {
                std::memcpy(out, &cmd, sizeof(cmd));
            }
        }
        if (resultPtr != 0u)
        {
            if (uint8_t *out = getMemPtr(rdram, resultPtr))
            {
                std::memcpy(out, &result, sizeof(result));
            }
        }

        mcTrace("Sync -> cmd=%d result=%d", cmd, result);
        // 1 = command finished in this runtime's immediate model.
        setReturnS32(ctx, 1);
    }

    void sceMcUnformat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
        const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));

        int32_t result = kMcResultNoEntry;
        {
            std::lock_guard<std::mutex> lock(g_mcStateMutex);
            if (isValidMcPortSlot(port, slot))
            {
                closeMcFilesForPortLocked(port);
                const std::filesystem::path root = getMcRootPath(port);
                std::error_code ec;
                std::filesystem::remove_all(root, ec);
                ec.clear();
                std::filesystem::create_directories(root, ec);
                if (!ec)
                {
                    McPortState &state = g_mcPorts[static_cast<size_t>(port)];
                    state.currentDir = "/";
                    state.formatted = false;
                    result = kMcResultSucceed;
                }
            }

            setMcCommandResultLocked(kMcCmdUnformat, result);
        }
        setReturnS32(ctx, 0);
    }

    void sceMcWrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int32_t fd = static_cast<int32_t>(getRegU32(ctx, 4));
        const uint32_t srcAddr = getRegU32(ctx, 5);
        const int32_t size = static_cast<int32_t>(getRegU32(ctx, 6));
        const uint8_t *src = (size > 0) ? getConstMemPtr(rdram, srcAddr) : nullptr;
        mcTrace("Write fd=%d src=0x%x size=%d (src=%p)", fd, srcAddr, size, (const void *)src);

        int32_t result = kMcResultNoEntry;
        {
            std::lock_guard<std::mutex> lock(g_mcStateMutex);
            auto it = g_mcFiles.find(fd);
            if (size <= 0)
            {
                result = 0;
            }
            else if (it == g_mcFiles.end() || !it->second.file)
            {
                result = kMcResultNoEntry;
            }
            else if (!src)
            {
                result = kMcResultDeniedPermit;
            }
            else
            {
                const size_t bytesWritten = std::fwrite(src, 1u, static_cast<size_t>(size), it->second.file);
                result = std::ferror(it->second.file) ? kMcResultDeniedPermit : static_cast<int32_t>(bytesWritten);
                if (!std::ferror(it->second.file))
                {
                    std::fflush(it->second.file);
                }
                else
                {
                    std::clearerr(it->second.file);
                }
            }

            setMcCommandResultLocked(kMcCmdWrite, result);
        }
        setReturnS32(ctx, 0);
    }

    void mcCallMessageTypeSe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void mcCheckReadStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void mcCheckReadStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void mcCheckWriteStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void mcCheckWriteStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void mcCreateConfigInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void mcCreateFileSelectWindow(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void mcCreateIconInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void mcCreateSaveFileInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void mcDispFileName(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void mcDispFileNumber(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void mcDisplayFileSelectWindow(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void mcDisplaySelectFileInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void mcDisplaySelectFileInfoMesCount(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void mcDispWindowCurSol(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void mcDispWindowFoundtion(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void mceGetInfoApdx(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void mceIntrReadFixAlign(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void mceStorePwd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void mcGetConfigCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, kCvMcConfigCapacityBytes);
    }

    void mcGetFileSelectWindowCursol(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, g_cvMcFileCursor);
    }

    void mcGetFreeCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, kCvMcFreeCapacityBytes);
    }

    void mcGetIconCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, kCvMcIconCapacityBytes);
    }

    void mcGetIconFileCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, kCvMcIconCapacityBytes);
    }

    void mcGetPortSelectDirInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void mcGetSaveFileCapacitySize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, kCvMcSaveCapacityBytes);
    }

    void mcGetStringEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t strAddr = getRegU32(ctx, 4);
        const std::string value = readPs2CStringBounded(rdram, runtime, strAddr, 1024);
        setReturnU32(ctx, strAddr + static_cast<uint32_t>(value.size()));
    }

    void mcMoveFileSelectWindowCursor(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const int32_t delta = static_cast<int32_t>(getRegU32(ctx, 5));
        g_cvMcFileCursor += delta;
        g_cvMcFileCursor = std::clamp(g_cvMcFileCursor, -1, 15);
        setReturnS32(ctx, 0);
    }

    void mcNewCreateConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void mcNewCreateIcon(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void mcNewCreateSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void mcReadIconData(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void mcReadStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void mcReadStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void mcSelectFileInfoInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_cvMcFileCursor = 0;
        setReturnS32(ctx, 1);
    }

    void mcSelectSaveFileCheck(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void mcSetFileSelectWindowCursol(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_cvMcFileCursor = static_cast<int32_t>(getRegU32(ctx, 5));
        g_cvMcFileCursor = std::clamp(g_cvMcFileCursor, -1, 15);
        setReturnS32(ctx, 0);
    }

    void mcSetFileSelectWindowCursolInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_cvMcFileCursor = 0;
        setReturnS32(ctx, 0);
    }

    void mcSetStringSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void mcSetTyepWriteMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void mcWriteIconData(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void mcWriteStartConfigFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }

    void mcWriteStartSaveFile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 1);
    }
}

// [statesync] The memory-card HLE's host state: what sceMcSync will answer (the last command and its
// result), the per-port latches (the first GetInfo after boot answers "card changed"), the fd counter
// and the open files (path relative to the card root + position, reopened on restore). A peer that
// adopted our RAM mid-transaction otherwise answers its own last result and libmc's state machine
// (the table at 0x331d80) takes a different step ~30 frames after a sync -- the first netplay desync.
namespace
{
    struct McSnap
    {
        int32_t nextFd = 1, lastCmd = 0, lastResult = 0, cvCursor = 0;
        struct Port { std::string currentDir; bool formatted, infoQueried; } ports[2];
        struct File { int32_t fd, port; std::string rel; int64_t pos; };
        std::vector<File> files;
    };
}
extern "C" void *ps2xMcStateCapture()
{
    McSnap *s = new McSnap();
    std::lock_guard<std::mutex> lock(ps2_stubs::g_mcStateMutex);
    s->nextFd = ps2_stubs::g_mcNextFd; s->lastCmd = ps2_stubs::g_mcLastCmd; s->lastResult = ps2_stubs::g_mcLastResult; s->cvCursor = ps2_stubs::g_cvMcFileCursor;
    for (int i = 0; i < 2; ++i) { s->ports[i].currentDir = ps2_stubs::g_mcPorts[i].currentDir; s->ports[i].formatted = ps2_stubs::g_mcPorts[i].formatted; s->ports[i].infoQueried = ps2_stubs::g_mcPorts[i].infoQueried; }
    for (const auto &kv : ps2_stubs::g_mcFiles)
    {
        const ps2_stubs::McOpenFile &f = kv.second;
        std::error_code ec;
        const std::filesystem::path root = ps2_stubs::getMcRootPath(f.port);
        const std::filesystem::path rel = f.hostPath.lexically_relative(root);
        const long pos = f.file ? std::ftell(f.file) : 0L;
        s->files.push_back(McSnap::File{ kv.first, f.port, (rel.empty() ? f.hostPath : rel).generic_string(), pos < 0 ? 0 : (int64_t)pos });
    }
    return s;
}
extern "C" bool ps2xMcStateRestore(void *h)
{
    const McSnap *s = static_cast<const McSnap *>(h);
    if (!s) return false;
    std::lock_guard<std::mutex> lock(ps2_stubs::g_mcStateMutex);
    ps2_stubs::g_mcNextFd = s->nextFd; ps2_stubs::g_mcLastCmd = s->lastCmd; ps2_stubs::g_mcLastResult = s->lastResult; ps2_stubs::g_cvMcFileCursor = s->cvCursor;
    for (int i = 0; i < 2; ++i) { ps2_stubs::g_mcPorts[i].currentDir = s->ports[i].currentDir; ps2_stubs::g_mcPorts[i].formatted = s->ports[i].formatted; ps2_stubs::g_mcPorts[i].infoQueried = s->ports[i].infoQueried; }
    for (auto &kv : ps2_stubs::g_mcFiles) if (kv.second.file) std::fclose(kv.second.file);
    ps2_stubs::g_mcFiles.clear();
    for (const McSnap::File &f : s->files)
    {
        ps2_stubs::McOpenFile of; of.port = f.port;
        of.hostPath = (ps2_stubs::getMcRootPath(f.port) / f.rel).lexically_normal();
        of.file = std::fopen(of.hostPath.string().c_str(), "r+b");
        if (!of.file) of.file = std::fopen(of.hostPath.string().c_str(), "rb");
        if (of.file) std::fseek(of.file, (long)f.pos, SEEK_SET);
        else std::fprintf(stderr, "[statesync] mc: cannot reopen fd %d (%s)\n", f.fd, of.hostPath.string().c_str());
        ps2_stubs::g_mcFiles[f.fd] = of;
    }
    return true;
}
extern "C" void ps2xMcStateFree(void *h) { delete static_cast<McSnap *>(h); }
extern "C" bool ps2xMcStateSerialize(const void *h, std::vector<uint8_t> &out)
{
    const McSnap *s = static_cast<const McSnap *>(h);
    if (!s) return false;
    Ps2xByteW w(out);
    w.u32(0x4d434131u);   // 'MCA1'
    w.pod(s->nextFd); w.pod(s->lastCmd); w.pod(s->lastResult); w.pod(s->cvCursor);
    for (int i = 0; i < 2; ++i) { w.str(s->ports[i].currentDir); w.u8(s->ports[i].formatted); w.u8(s->ports[i].infoQueried); }
    w.u64(s->files.size());
    for (const auto &f : s->files) { w.pod(f.fd); w.pod(f.port); w.str(f.rel); w.i64(f.pos); }
    return true;
}
extern "C" void *ps2xMcStateDeserialize(const uint8_t *data, size_t n, size_t *used)
{
    Ps2xByteR r(data, n);
    if (r.u32() != 0x4d434131u) return nullptr;
    McSnap *s = new McSnap();
    s->nextFd = r.pod<int32_t>(); s->lastCmd = r.pod<int32_t>(); s->lastResult = r.pod<int32_t>(); s->cvCursor = r.pod<int32_t>();
    for (int i = 0; i < 2; ++i) { s->ports[i].currentDir = r.str(); s->ports[i].formatted = r.u8() != 0; s->ports[i].infoQueried = r.u8() != 0; }
    const size_t k = r.count(20);
    for (size_t i = 0; i < k && r.ok; ++i) { McSnap::File f; f.fd = r.pod<int32_t>(); f.port = r.pod<int32_t>(); f.rel = r.str(); f.pos = r.i64(); s->files.push_back(f); }
    if (!r.ok) { delete s; return nullptr; }
    if (used) *used = (size_t)(r.p - data);
    return s;
}
