#pragma once

#include "frontend/fe_hw.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

// Install wizard: the user's own disc dump in, validated game data out. Same flow as the Qt
// launcher's install_wizard_dialog (validate -> stream the ISO into data/ -> convert the AFS
// containers to folders -> summary), with std::thread workers and ImGui instead of QThread.
//
// Threading: the workers publish through a mutex (phase/status/error/verified) plus a few
// atomics for the progress counters, so the UI never reads a std::string a worker is writing.
class InstallWizard
{
public:
    explicit InstallWizard(std::filesystem::path exeDir) : m_exeDir(std::move(exeDir)) {}
    ~InstallWizard();

    InstallWizard(const InstallWizard &) = delete;
    InstallWizard &operator=(const InstallWizard &) = delete;

    // dataPresent only changes the first page: with data installed the wizard starts on the
    // dump page (that is how the launcher used it as a reinstall entry point).
    void begin(bool dataPresent);
    void draw();

    // The shell uses this when it opens the wizard on the user's behalf because nothing is
    // installed: the browser is the only useful page, the welcome text is not.
    void startAtLocatePage();

    bool closing() const { return m_closing; }
    void requestClose() { m_closing = true; }
    bool installed() const { return m_installed; }

    // Installs a texture pack archive (the browsable alternative to the old download).
    void installPack(const std::string &archivePath);
    void startVerify(const std::string &dumpPath);

    // The picker lives in the shell (it owns the ImGui popup), so the wizard just says what it
    // wants picked and receives the answer.
    enum class PickKind
    {
        None,
        Dump,
        Pack
    };
    PickKind wantedPick() const { return m_wantedPick; }
    void clearWantedPick() { m_wantedPick = PickKind::None; }
    void requestPick(PickKind kind) { m_wantedPick = kind; }
    // Set when the wizard only exists to install a texture pack, so the shell knows which
    // callback the picker's answer belongs to.
    void setPackMode(bool on) { m_packMode = on; }
    bool packMode() const { return m_packMode; }
    void onPickedDump(const std::string &path);
    void onPickedPack(const std::string &path);

private:
    enum class Page
    {
        Welcome,
        Locate,
        Install,
        Summary
    };
    enum class Phase
    {
        Idle,
        Busy,
        Done,
        Failed
    };

    void logInstall(const std::string &line);
    void goTo(Page page);
    void startInstall();
    void startPack(const std::string &archivePath);
    void pollWorkers();
    void joinWorker();
    void applySummary();

    void workerVerify();
    void workerExtract();
    void workerAfs();
    void workerPack(const std::string &archivePath, std::string archive);
    void resolveInnerImage(const std::string &dumpPath, std::string &out, std::string &err);

    void setStatus(const std::string &s);
    std::string status() const;

    std::filesystem::path m_exeDir;
    std::filesystem::path m_dataDir;
    std::filesystem::path m_tempDir;

    mutable std::mutex m_mutex;
    Page m_page = Page::Welcome;
    Phase m_phase = Phase::Idle;
    bool m_reinstall = false;
    bool m_installed = false;
    bool m_closing = false;

    std::string m_dumpPath;
    std::string m_isoPath;
    std::string m_status;
    std::string m_error;
    std::string m_doneLabel;
    bool m_verified = false;

    std::thread m_worker;
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_workerRunning{false};
    std::atomic<std::uint64_t> m_done{0};
    std::atomic<std::uint64_t> m_total{0};
    bool m_progressKnown = false;

    std::thread m_bench;
    std::atomic<double> m_cpuR{0.0};

    bool m_pickDump = false;
    bool m_pickPack = false;
    bool m_packMode = false;
    bool m_testAutoStart = false;
    PickKind m_wantedPick = PickKind::None;
    std::chrono::steady_clock::time_point m_installStart{};
    bool m_installTiming = false;

    hw::Recommendation m_rec;
    bool m_recValid = false;
};
