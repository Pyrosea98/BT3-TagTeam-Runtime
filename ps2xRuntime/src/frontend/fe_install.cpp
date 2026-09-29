#include "frontend/fe_install.h"

#include "frontend/archive_x.h"
#include "frontend/fe_afs.h"
#include "frontend/fe_hash.h"
#include "frontend/fe_iso9660.h"
#include "frontend/fe_ui.h"

#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

namespace
{
    const char *const kDumpFilters[] = {
        "Game dump (*.iso;*.img;*.7z;*.zip;*.rar;*.tar;*.tar.gz;*.tgz)",
        "*.iso;*.img;*.7z;*.zip;*.rar;*.tar;*.tar.gz;*.tgz",
        "All files (*)", "*"
    };
    const char *const kPackFilters[] = {
        "Texture pack (*.7z;*.zip)", "*.7z;*.zip",
        "All files (*)", "*"
    };

    bool iequalsAscii(const std::string &a, const char *b)
    {
        std::size_t i = 0;
        for (; i < a.size() && b[i]; ++i)
        {
            char x = a[i], y = b[i];
            if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
            if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
            if (x != y)
                return false;
        }
        return i == a.size() && b[i] == '\0';
    }

    bool endsWithNoCase(const std::string &s, const char *suffix)
    {
        const std::size_t n = std::strlen(suffix);
        return s.size() >= n && iequalsAscii(s.substr(s.size() - n), suffix);
    }

    std::string fmtMb(std::uint64_t bytes)
    {
        return std::to_string(bytes / (1024ull * 1024ull));
    }

    std::string fmtEta(std::uint64_t done, std::uint64_t total, std::int64_t elapsedMs)
    {
        if (done == 0 || total <= done || elapsedMs <= 0)
            return {};
        const double rate = (double)done / (double)elapsedMs;
        if (rate <= 0.0)
            return {};
        const std::int64_t remainMs = (std::int64_t)((double)(total - done) / rate);
        const std::int64_t sec = remainMs / 1000;
        char buf[64];
        std::snprintf(buf, sizeof buf, "~%lld:%02lld left", (long long)(sec / 60),
                      (long long)(sec % 60));
        return buf;
    }

    std::string findImageRecursive(const std::filesystem::path &root, int maxDepth)
    {
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(root, ec), end;
             it != end && !ec; it.increment(ec))
        {
            if (it->is_directory(ec))
            {
                if (std::distance(it->path().begin(), root.begin()) > maxDepth)
                    it.disable_recursion_pending();
                continue;
            }
            const std::string name = it->path().filename().string();
            if (endsWithNoCase(name, ".iso") || endsWithNoCase(name, ".img"))
                return it->path().string();
        }
        return {};
    }

    std::vector<std::filesystem::path> findAfsContainers(const std::filesystem::path &dataDir)
    {
        std::vector<std::filesystem::path> out;
        std::error_code ec;
        if (!std::filesystem::is_directory(dataDir, ec))
            return out;
        for (std::filesystem::recursive_directory_iterator it(dataDir, ec), end;
             it != end && !ec; it.increment(ec))
        {
            if (it->is_directory(ec))
                continue;
            const std::string name = it->path().filename().string();
            if (name.size() > 4 && name.compare(0, 6, "PZS3US") == 0 && endsWithNoCase(name, ".AFS"))
                out.push_back(it->path());
        }
        std::sort(out.begin(), out.end());
        return out;
    }
}

InstallWizard::~InstallWizard()
{
    m_cancel = true;
    joinWorker();
    if (m_bench.joinable())
        m_bench.join();
}

void InstallWizard::setStatus(const std::string &s)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status = s;
}

std::string InstallWizard::status() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

void InstallWizard::logInstall(const std::string &line)
{
    std::error_code ec;
    const std::filesystem::path dir = m_exeDir / "logs";
    std::filesystem::create_directories(dir, ec);

    const std::time_t t = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
    std::FILE *f = std::fopen((dir / "install.log").string().c_str(), "ab");
    if (!f)
        return;
    std::fprintf(f, "%s  %s\n", stamp, line.c_str());
    std::fclose(f);
}

void InstallWizard::begin(bool dataPresent)
{
    m_dataDir = m_exeDir / "data";
    m_reinstall = dataPresent;
    m_cancel = false;
    m_progressKnown = false;
    m_done = 0;
    m_total = 0;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_page = dataPresent ? Page::Locate : Page::Welcome;
        m_phase = Phase::Idle;
        m_status.clear();
        m_error.clear();
        m_doneLabel.clear();
        m_verified = false;
        m_installed = false;
        m_dumpPath.clear();
        m_isoPath.clear();
    }
    logInstall(dataPresent ? "wizard opened (reinstall=1)" : "wizard opened (reinstall=0)");

    // The single-thread score feeds the summary tier; run it while the user picks a dump.
    if (m_bench.joinable())
        m_bench.join();
    m_bench = std::thread([this] { m_cpuR = hw::benchSingleThreadR(); });

    // PS2X_INSTALL_TEST=<dump>: drive the whole verify -> install path without a human, so
    // the extraction can be checked headlessly. Inert unless the variable is set.
        if (const char *test = std::getenv("PS2X_INSTALL_TEST"))
            if (test[0])
            {
                startVerify(test);
                m_testAutoStart = true;
            }
}

void InstallWizard::startAtLocatePage()
{
    goTo(Page::Locate);
    m_reinstall = false;
    logInstall("wizard opened on the dump page (nothing installed)");
}

void InstallWizard::goTo(Page page)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_page = page;
}

void InstallWizard::joinWorker()
{
    if (m_worker.joinable())
        m_worker.join();
}

void InstallWizard::onPickedDump(const std::string &path)
{
    startVerify(path);
}

void InstallWizard::onPickedPack(const std::string &path)
{
    installPack(path);
}

void InstallWizard::pollWorkers()
{
    if (m_workerRunning.load(std::memory_order_relaxed))
        return;
    if (m_worker.joinable())
        m_worker.join();

    if (m_testAutoStart)
    {
        bool start = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_verified && !m_isoPath.empty())
            {
                m_testAutoStart = false;
                start = true;
            }
            else if (m_phase == Phase::Failed)
            {
                m_testAutoStart = false;
            }
        }
        if (start)
            startInstall();
    }
}

void InstallWizard::startVerify(const std::string &dumpPath)
{
    joinWorker();
    m_cancel = false;
    m_progressKnown = false;
    m_done = 0;
    m_total = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_dumpPath = dumpPath;
        m_verified = false;
        m_error.clear();
        m_isoPath.clear();
        m_status = "Opening the dump...";
        m_phase = Phase::Busy;
    }
    m_workerRunning = true;
    m_worker = std::thread([this] { workerVerify(); });
}

void InstallWizard::resolveInnerImage(const std::string &dumpPath, std::string &out, std::string &err)
{
    std::error_code ec;
    m_tempDir = std::filesystem::temp_directory_path(ec) / "bt3_install_unpack";
    std::filesystem::remove_all(m_tempDir, ec);
    std::filesystem::create_directories(m_tempDir, ec);

    setStatus("Extracting the compressed file...");
    ArchiveExtractJob job;
    job.start(dumpPath, m_tempDir.string());
    while (!job.finished())
    {
        m_done = job.bytesDone();
        m_total = job.bytesTotal();
        m_progressKnown = job.bytesTotal() > 0;
        if (m_cancel.load())
        {
            job.requestCancel();
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
    job.join();
    m_progressKnown = false;

    if (!job.ok())
    {
        err = "Could not decompress. Try extracting the ISO by hand.";
        return;
    }
    out = findImageRecursive(m_tempDir, 3);
    if (out.empty())
        err = "The file does not contain a disc image (ISO/IMG).";
}

void InstallWizard::workerVerify()
{
    const bool isImage = endsWithNoCase(m_dumpPath, ".iso") || endsWithNoCase(m_dumpPath, ".img");
    logInstall("verify start: '" + m_dumpPath + "' isImage=" + (isImage ? "1" : "0"));

    std::string iso = m_dumpPath;
    if (!isImage)
    {
        std::string err;
        resolveInnerImage(m_dumpPath, iso, err);
        if (iso.empty())
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_error = err.empty() ? "No disc image found inside." : err;
            m_phase = Phase::Failed;
            logInstall("verify FAIL: " + m_error);
            m_workerRunning = false;
            return;
        }
    }

    setStatus("Verifying the disc (SLUS_216.78)...");
    const bool ok = DiscVerify::verifySlusFromIso(iso);
    logInstall(std::string("verify ") + (ok ? "OK" : "FAIL") + ": " + iso +
               " sha256=" + DiscVerify::kExpectedDiscElfSha256);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_isoPath = iso;
        m_verified = ok;
        if (!ok)
        {
            m_error = "Could not verify the disc. Is it the right version?";
            m_phase = Phase::Failed;
        }
        else
        {
            m_status = "Disc verified";
            m_phase = Phase::Idle;
        }
    }
    m_workerRunning = false;
}

void InstallWizard::startInstall()
{
    std::string iso;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_verified || m_isoPath.empty())
            return;
        iso = m_isoPath;
        m_error.clear();
        m_doneLabel.clear();
        m_page = Page::Install;
        m_phase = Phase::Busy;
    }
    joinWorker();
    m_cancel = false;
    m_progressKnown = false;
    m_done = 0;
    m_total = 0;
    m_installStart = std::chrono::steady_clock::now();
    m_installTiming = true;
    logInstall("extract start: " + iso);
    m_workerRunning = true;
    m_worker = std::thread([this] { workerExtract(); });
}

void InstallWizard::workerExtract()
{
    auto fail = [this](const std::string &msg)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_error = msg;
        m_phase = Phase::Failed;
        m_progressKnown = false;
        m_workerRunning = false;
    };

    Iso9660 iso;
    if (!iso.open(m_isoPath))
    {
        fail("Could not read the ISO image: " + iso.error());
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(m_dataDir, ec);

    std::uint64_t total = 0;
    for (const Iso9660::File &f : iso.files())
        if (!f.dir)
            total += f.size();
    m_total = total;
    m_progressKnown = total > 0;

    std::uint64_t done = 0;
    for (const Iso9660::File &f : iso.files())
    {
        if (f.dir)
            continue;
        if (m_cancel.load())
        {
            fail("Cancelled.");
            return;
        }

        const std::filesystem::path out = m_dataDir / f.path;
        std::filesystem::create_directories(out.parent_path(), ec);
        if (ec)
        {
            fail("Could not create " + out.parent_path().string());
            return;
        }

        setStatus("Extracting " + f.path);
        std::FILE *fp = std::fopen(out.string().c_str(), "wb");
        if (!fp)
        {
            fail("Could not write " + out.string());
            return;
        }
        const std::int64_t got = iso.readFile(
            f, [&](const char *data, std::size_t size)
            {
                std::fwrite(data, 1, size, fp);
                done += size;
                m_done.store(done, std::memory_order_relaxed);
            });
        std::fclose(fp);
        if (got < 0)
        {
            fail("Read error while extracting " + f.path);
            return;
        }
    }

    m_progressKnown = false;
    if (!findAfsContainers(m_dataDir).empty())
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_doneLabel = "Converting the game data to folders...";
        }
        workerAfs();
        return;
    }
    applySummary();
}

void InstallWizard::workerAfs()
{
    const std::vector<std::filesystem::path> afs = findAfsContainers(m_dataDir);
    std::uint64_t totalBytes = 0;
    for (const auto &p : afs)
    {
        std::error_code ec;
        const std::uintmax_t sz = std::filesystem::file_size(p, ec);
        if (!ec)
            totalBytes += 2ull * (std::uint64_t)sz; // one pass writes, one verifies
    }
    m_total = totalBytes;
    m_progressKnown = totalBytes > 0;
    m_done = 0;

    const std::string nameTableDir = (m_exeDir / "assets").string();
    std::uint64_t base = 0;
    const std::uint64_t tick = totalBytes / 500 + 1;

    for (const auto &p : afs)
    {
        if (m_cancel.load())
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_error = "Cancelled.";
            m_phase = Phase::Failed;
            m_progressKnown = false;
            m_workerRunning = false;
            return;
        }
        std::error_code ec;
        const std::uintmax_t sz = std::filesystem::file_size(p, ec);
        const std::uint64_t fileTotal = ec ? 0 : 2ull * (std::uint64_t)sz;
        const std::uint64_t localBase = base;

        setStatus("Converting " + p.filename().string());
        const AfsConvertResult r = convertAfsToFolder(
            p.string(), p.parent_path().string(),
            [this](const std::string &s) { setStatus(s); },
            [this, localBase, tick](std::uint64_t d, std::uint64_t)
            {
                const std::uint64_t cur = localBase + d;
                if (cur >= m_done.load(std::memory_order_relaxed) + tick)
                    m_done.store(cur, std::memory_order_relaxed);
            },
            nameTableDir);

        if (!r.ok)
        {
            logInstall("afs FAIL: " + r.error);
            std::lock_guard<std::mutex> lock(m_mutex);
            m_error = r.error;
            m_phase = Phase::Failed;
            m_progressKnown = false;
            m_workerRunning = false;
            return;
        }

        base += fileTotal;
        m_done.store(base, std::memory_order_relaxed);
        setStatus("Deleting " + p.filename().string());
        std::filesystem::remove(p, ec);
    }

    logInstall("afs done: ok=1");
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_doneLabel = "Install complete. Game data converted to folders.";
    }
    applySummary();
}

void InstallWizard::applySummary()
{
    if (m_bench.joinable())
        m_bench.join();
    m_rec = hw::recommend(hw::detect(), m_cpuR.load());
    m_recValid = true;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_doneLabel.empty())
            m_doneLabel = "Install complete. Disc verified.";
        m_installed = true;
        m_phase = Phase::Done;
        m_page = Page::Summary;
    }
    logInstall("result: ok=1 " + m_doneLabel);
}

void InstallWizard::installPack(const std::string &archivePath)
{
    joinWorker();
    m_cancel = false;
    m_progressKnown = false;
    m_done = 0;
    m_total = 0;
    m_installStart = std::chrono::steady_clock::now();
    m_installTiming = true;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_error.clear();
        m_status = "Installing the texture pack...";
        m_doneLabel = "Installing the texture pack...";
        m_page = Page::Install;
        m_phase = Phase::Busy;
    }
    logInstall("pack install start: " + archivePath);
    m_workerRunning = true;
    m_worker = std::thread([this, archivePath] { workerPack(archivePath, archivePath); });
}

void InstallWizard::workerPack(const std::string &archivePath, std::string archive)
{
    const std::filesystem::path packDir = m_dataDir / "Textures";
    std::error_code ec;
    std::filesystem::create_directories(packDir, ec);

    ArchiveExtractJob job;
    job.start(archivePath, packDir.string());
    while (!job.finished())
    {
        m_done = job.bytesDone();
        m_total = job.bytesTotal();
        m_progressKnown = job.bytesTotal() > 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_status = "Extracting the pack (" + fmtMb(job.bytesDone()) + " MB)...";
        }
        if (m_cancel.load())
            job.requestCancel();
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
    job.join();
    m_progressKnown = false;
    (void)archive;

    if (!job.ok())
    {
        logInstall("pack install FAIL");
        std::lock_guard<std::mutex> lock(m_mutex);
        m_error = "Could not install the pack: " + job.error();
        m_phase = Phase::Failed;
        m_workerRunning = false;
        return;
    }

    logInstall("pack install done: ok=1");
    std::lock_guard<std::mutex> lock(m_mutex);
    m_doneLabel = "Texture pack installed in " + packDir.string();
    m_installed = true;
    m_phase = Phase::Done;
    m_page = Page::Summary;
    m_workerRunning = false;
}

void InstallWizard::draw()
{
    pollWorkers();

    Page page;
    Phase phase;
    bool verified;
    bool installed;
    std::string dumpPath;
    std::string st;
    std::string err;
    std::string doneLabel;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        page = m_page;
        phase = m_phase;
        verified = m_verified;
        installed = m_installed;
        dumpPath = m_dumpPath;
        st = m_status;
        err = m_error;
        doneLabel = m_doneLabel;
    }

    fe::sectionHeader("INSTALL WIZARD");

    switch (page)
    {
    case Page::Welcome:
    {
        ImGui::TextWrapped(
            "Before playing you have to install the game files from your own disc image. "
            "This is done once; your saves and settings are not touched.");
        ImGui::Spacing();
        ImGui::BulletText("1. Point the wizard at your dump (ISO or compressed archive).");
        ImGui::BulletText("2. We verify the disc and extract the data into this folder.");
        ImGui::BulletText("3. We convert the AFS files into folders, which is how the game reads them.");
        ImGui::Spacing();
        if (fe::primaryButton("NEXT", ImVec2(160.0f, 30.0f)))
            goTo(Page::Locate);
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            requestClose();
        break;
    }
    case Page::Locate:
    {
        ImGui::TextWrapped("Select the disc image:");
        ImGui::Spacing();
        // [isopatch] The verify below is a SHA-256 of the boot ELF, so a dump that is the right
        // title and region but code-patched inside .bin/.elf fails here with no explanation. Say so
        // up front: the runtime recompiles BIN/DBZP.BIN and the overlay tables are keyed by raw
        // address, so patching code moves every address after it and the recompiled data no longer
        // matches the image the guest loads.
        ImGui::TextColored(fe::warnCol(), "%s", "Note: the dump must be the correct title and region (USA).");
        ImGui::TextWrapped("%s",
                           "If it is code-patched inside the .bin or the .elf it will not work: "
                           "the game is recompiled, so a patch that moves an instruction invalidates "
                           "the address tables. The other patches (textures, disc patching, "
                           "translations) do not touch code and do work.");
        ImGui::Spacing();
        if (dumpPath.empty())
            ImGui::TextDisabled("No file selected");
        else
            ImGui::TextWrapped("%s", dumpPath.c_str());
        ImGui::Spacing();

        if (phase == Phase::Busy)
            ImGui::TextColored(fe::warnCol(), "%s", st.c_str());
        else if (verified)
            ImGui::TextColored(fe::okCol(), "Disc verified (SHA-256 %s...)",
                               std::string(DiscVerify::kExpectedDiscElfSha256).substr(0, 16).c_str());
        else if (!err.empty())
            ImGui::TextColored(fe::badCol(), "%s", err.c_str());
        else
            ImGui::TextDisabled("Waiting for an image...");

        ImGui::Spacing();
        if (ImGui::Button("Select image..."))
            m_wantedPick = PickKind::Dump;
        ImGui::SameLine();
        ImGui::BeginDisabled(dumpPath.empty() || phase == Phase::Busy);
        if (ImGui::Button("Verify again"))
            startVerify(dumpPath);
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::BeginDisabled(!verified || phase == Phase::Busy);
        if (fe::primaryButton("INSTALL", ImVec2(160.0f, 30.0f), verified))
            startInstall();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Back"))
        {
            if (m_reinstall)
                requestClose();
            else
                goTo(Page::Welcome);
        }
        break;
    }
    case Page::Install:
    {
        ImGui::TextWrapped("%s", doneLabel.c_str());
        ImGui::Spacing();
        if (phase == Phase::Failed)
        {
            ImGui::TextColored(fe::badCol(), "%s", err.c_str());
            if (ImGui::Button("Reintentar"))
            {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_error.clear();
                }
                startInstall();
            }
        }
        else
        {
            const std::uint64_t done = m_done.load();
            const std::uint64_t total = m_total.load();
            const float frac = total > 0 ? (float)((double)done / (double)total) : 0.0f;
            ImGui::ProgressBar(frac, ImVec2(-1.0f, 0.0f));
            char line[224];
            const std::int64_t elapsed = m_installTiming
                ? (std::int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - m_installStart)
                      .count()
                : 0;
            const std::string eta = fmtEta(done, total, elapsed);
            if (!eta.empty())
                std::snprintf(line, sizeof line, "%s MB / %s MB   -   %s   -   %s",
                              fmtMb(done).c_str(), fmtMb(total).c_str(), st.c_str(), eta.c_str());
            else
                std::snprintf(line, sizeof line, "%s MB / %s MB   -   %s", fmtMb(done).c_str(),
                              fmtMb(total).c_str(), st.c_str());
            ImGui::TextDisabled("%s", line);
            if (ImGui::Button("Cancel"))
                m_cancel = true;
        }
        break;
    }
    case Page::Summary:
    {
        ImGui::TextColored(fe::okCol(), "%s", doneLabel.c_str());
        ImGui::Spacing();
        ImGui::BulletText("Disc verified (SLUS_216.78)");
        ImGui::BulletText("Data in %s", (m_exeDir / "data").string().c_str());
        if (installed)
            ImGui::Spacing();
        if (m_recValid)
        {
            fe::sectionHeader("YOUR HARDWARE");
            ImGui::TextWrapped("%s", hw::summary(hw::detect()).c_str());
            fe::sectionHeader("RECOMMENDED SETTINGS");
            char line[256];
            std::snprintf(line, sizeof line, "%s  -  %dx render  -  pack %s  -  %d fps",
                          m_rec.tierName.c_str(), m_rec.renderScale,
                          m_rec.texPackFull ? "completo" : "leve", m_rec.fps60 ? 60 : 30);
            ImGui::TextWrapped("%s", line);
        }
        ImGui::Spacing();
        if (fe::primaryButton("LISTO", ImVec2(160.0f, 30.0f)))
            requestClose();
        break;
    }
    }
}
