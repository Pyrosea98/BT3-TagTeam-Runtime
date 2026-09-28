#include "app_paths.h"
#include "launcher_window.h"

#include "dbz_theme.h"
#include "hardware_probe.h"
#include "install_wizard_dialog.h"
#include "iso9660.h"
#include "settings_dialog.h"
#include "settings_manager.h"
#include "tex_install_dialog.h"
#include "view_host.h"

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QEasingCurve>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QShowEvent>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <cstdio>

namespace
{
    // BT3SELFX footer: the last 32 bytes are "BT3SELFX" magic + payload info.
    bool isSelfExtractElf(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return false;
        if (f.size() < 32)
            return false;
        if (!f.seek(f.size() - 32))
            return false;
        QByteArray tail = f.read(8);
        return tail == QByteArray("BT3SELFX");
    }
} // namespace

LauncherWindow::LauncherWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Dragon Ball Budokai Tenkaichi 3 Launcher"));
    resize(1080, 640);
    setMinimumSize(820, 500);

    // The launcher lives in the deploy root; savedata/ is the shared settings dir.
    m_savedataDir = QDir(apppaths::userRoot()).filePath(QStringLiteral("savedata"));
    m_dataDir = QDir(apppaths::userRoot()).filePath(QStringLiteral("data"));

    // Resolve the game ELF next to the launcher binary.
    const QDir appDir(QApplication::applicationDirPath());
    static const char *kCandidates[] = {
        "Dragon Ball - Budokai Tenkaichi 3",
        "Dragon Ball Budokai Tenkaichi 3",
        "SLUS-216.78",
    };
    for (const char *c : kCandidates)
    {
        const QString p = appDir.filePath(QString::fromLatin1(c));
        if (QFile::exists(p) && isSelfExtractElf(p))
        {
            m_gameElf = p;
            break;
        }
    }

    // Background: deploy assets/background.png if present, else a DBZ gradient.
    const QString bg = QDir(apppaths::assets()).filePath(QStringLiteral("background.png"));
    if (QFile::exists(bg))
        m_bgPath = bg;

    // Window/taskbar icon from the same asset tree.
    const QIcon appIcon(QDir(apppaths::assets()).filePath(QStringLiteral("icon.png")));
    if (!appIcon.isNull())
        setWindowIcon(appIcon);

    // Bottom bar with PLAY (left) + SETTINGS (right).
    auto *bottomBar = new QWidget(this);
    bottomBar->setObjectName(QStringLiteral("bottomBar"));
    // Opaque bar: solid background over the image area, only a top edge line.
    bottomBar->setStyleSheet(QStringLiteral(
        "QWidget#bottomBar { background-color: #0b0f13; border-top: 1px solid #1e2830; }"));
    m_bottomBar = bottomBar;

    auto *barCol = new QVBoxLayout(bottomBar);
    barCol->setContentsMargins(24, 12, 24, 10);
    barCol->setSpacing(8);
    auto *barLayout = new QHBoxLayout;
    barLayout->setContentsMargins(0, 0, 0, 0);

    m_play = new QPushButton(QStringLiteral("PLAY"), bottomBar);
    m_play->setObjectName(QStringLiteral("playButton"));
    m_play->setCursor(Qt::PointingHandCursor);
    m_play->setFixedHeight(58);

    m_settings = new QPushButton(QStringLiteral("SETTINGS"), bottomBar);
    m_settings->setObjectName(QStringLiteral("settingsButton"));
    m_settings->setCursor(Qt::PointingHandCursor);
    m_settings->setFixedHeight(50);

    barLayout->addWidget(m_play, 0, Qt::AlignVCenter);
    barLayout->addStretch(1);
    barLayout->addWidget(m_settings, 0, Qt::AlignVCenter);
    barCol->addLayout(barLayout);

    // [hwprobe] bottom row: detected specs (left, under PLAY) | game-data status
    // (right, under SETTINGS). Small and unobtrusive.
    auto *subRow = new QHBoxLayout;
    subRow->setContentsMargins(2, 0, 2, 0);
    m_specs = new QLabel(bottomBar);
    m_specs->setObjectName(QStringLiteral("specsLabel"));
    m_specs->setStyleSheet(QStringLiteral("color: #6b7280; background: transparent; font-size: 11px;"));
    m_hw = hw::detect();
    m_specs->setText(hw::summary(m_hw));
    m_specs->setToolTip(QStringLiteral("Detected hardware"));
    // [tier] Silent single-thread benchmark; the specs banner gains a tier chip.
    m_benchThread = QThread::create([this] { m_cpuR.store(hw::benchSingleThreadR()); });
    connect(m_benchThread, &QThread::finished, this, [this] {
        if (!m_specs)
            return;
        const hw::Recommendation r = hw::recommend(m_hw, m_cpuR.load());
        m_specs->setText(QStringLiteral("%1   \u00b7   [%2]").arg(hw::summary(m_hw), r.tierName));
        m_specs->setToolTip(QStringLiteral("Detected hardware \u2014 recommended tier: %1").arg(r.tierName));
    });
    m_benchThread->start();
    m_hint = new QLabel(bottomBar);
    m_hint->setObjectName(QStringLiteral("hintLabel"));
    m_hint->setStyleSheet(QStringLiteral("color: #9999b3; background: transparent; font-size: 11px;"));
    subRow->addWidget(m_specs, 0, Qt::AlignLeft | Qt::AlignVCenter);
    subRow->addStretch(1);
    subRow->addWidget(m_hint, 0, Qt::AlignRight | Qt::AlignVCenter);
    barCol->addLayout(subRow);

    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("launcherRoot"));
    // Keep the central container transparent so the background image painted
    // in paintEvent() actually shows through (the global QSS otherwise paints
    // an opaque WindowBg over it).
    root->setAttribute(Qt::WA_TranslucentBackground, true);
    root->setStyleSheet(QStringLiteral("QWidget#launcherRoot { background: transparent; }"));
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addStretch(1);
    layout->addWidget(bottomBar);
    // [inwindow] The launcher is page 0 of a stack; views (settings, wizard, ...)
    // become the other pages and replace the whole window content. The stack itself
    // must be transparent so the window's paintEvent() background shows through on
    // page 0 (the global QSS otherwise paints an opaque background over it).
    m_stack = new QStackedWidget(this);
    m_stack->setStyleSheet(QStringLiteral("QStackedWidget { background: transparent; }"));
    m_stack->addWidget(root);
    setCentralWidget(m_stack);
    // [inwindow] Let nested views (Video-tab dialogs) push/pop pages.
    viewhost::setHandlers([this](QWidget *v) { showView(v); }, [this] { popView(); });

    connect(m_play, &QPushButton::clicked, this, &LauncherWindow::onPlayClicked);
    connect(m_settings, &QPushButton::clicked, this, &LauncherWindow::onSettingsClicked);

    // [inwindow] Esc returns from a view to the previous page.
    {
        auto *esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        connect(esc, &QShortcut::activated, this, [this] {
            if (m_stack && m_stack->currentIndex() != 0)
                popView();
        });
    }

    checkGameData();

    // Seed settings manager from the shared savedata dir.
    SettingsManager::instance().setConfigDir(m_savedataDir);
    SettingsManager::instance().load();

    // [firstboot] No settings.toml yet -> pulse SETTINGS so a first-time user
    // is drawn to Settings > Misc (game data + texture pack), without a dialog
    // interrupting them.
    const bool firstBoot = !QFile::exists(SettingsManager::instance().configpath());
    if (firstBoot)
        startSettingsGlow();
}

LauncherWindow::~LauncherWindow()
{
    if (m_benchThread)
    {
        m_benchThread->wait(1000);
        m_benchThread->deleteLater();
    }
}

// Release deploy: no SELFX next to us, but a plain ps2EntryRunner from the
// same stage tree. Boot it directly with data/SLUS_216.78 instead of
// embedding a duplicate self-extracting runner (saves ~130 MiB payload).
// Re-run after the install wizard so a freshly created runner (direct-runner
// deploy) flips the hint straight to "Ready to Play" without a restart.
void LauncherWindow::resolveLaunchTarget()
{
    if (m_gameElf.isEmpty())
    {
#ifdef _WIN32
        const QString runner = QDir::cleanPath(
            QApplication::applicationDirPath() + QStringLiteral("/bt3-runner.exe"));
#else
        const QString runner = QDir::cleanPath(
            QApplication::applicationDirPath() + QStringLiteral("/bt3-runner"));
#endif
        if (QFile::exists(runner) && QFileInfo(runner).isExecutable())
            m_plainRunner = true;
        else
            m_plainRunner = false;
    }
}

QString LauncherWindow::findGameElf()
{
    const QDir appDir(QApplication::applicationDirPath());
    static const char *kCandidates[] = {
        "Dragon Ball - Budokai Tenkaichi 3",
        "Dragon Ball Budokai Tenkaichi 3",
        "SLUS-216.78",
    };
    for (const char *c : kCandidates)
    {
        const QString p = appDir.filePath(QString::fromLatin1(c));
        if (QFile::exists(p) && isSelfExtractElf(p))
            return p;
    }
    return QString();
}

void LauncherWindow::logVulkanFallback(const QString &msg)
{
    const QString logsDir = QDir(apppaths::userRoot()).filePath(QStringLiteral("logs"));
    QDir().mkpath(logsDir);
    QFile f(QDir(logsDir).filePath(QStringLiteral("vulkan-fallback.log")));
    if (f.open(QIODevice::Append | QIODevice::Text))
    {
        f.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
        f.write("  ");
        f.write(msg.toUtf8());
        f.write("\n");
    }
}

void LauncherWindow::onPlayClicked()
{
    if (!m_plainRunner && m_gameElf.isEmpty())
    {
        m_gameElf = findGameElf();
        if (m_gameElf.isEmpty())
            return;
    }

    // Game data must be present and validated before the runner can boot.
    if (!m_gameDataValid)
    {
        // The install wizard (in-window view) restores data on success; the user
        // presses PLAY again once it finishes.
        openInstallWizard();
        return;
    }

    // Save any pending settings so the game boots with the launcher's config.
    SettingsManager::instance().save();

    const QDir appDir(QApplication::applicationDirPath());

    QProcess *proc = new QProcess(nullptr);
    proc->setWorkingDirectory(apppaths::userRoot());
#if defined(Q_OS_MACOS)
    // Detached GUI applications do not inherit a useful terminal on macOS.
    // Keep the most recent runner diagnostics where users can attach them to a report.
    const QString logsDir = QDir(apppaths::userRoot()).filePath(QStringLiteral("logs"));
    QDir().mkpath(logsDir);
    proc->setStandardOutputFile(QDir(logsDir).filePath(QStringLiteral("game-latest.out")));
    proc->setStandardErrorFile(QDir(logsDir).filePath(QStringLiteral("game-latest.log")));
#endif

    if (m_plainRunner)
    {
        // Direct runner mode: point it at the extracted boot ELF and the
        // bundled library tree; it is already the real ps2EntryRunner.
#ifdef _WIN32
        proc->setProgram(appDir.filePath(QStringLiteral("bt3-runner.exe")));
#else
        proc->setProgram(appDir.filePath(QStringLiteral("bt3-runner")));
#endif
        const QString dataDir = m_dataDir;
        proc->setArguments({QDir(dataDir).filePath(QStringLiteral("SLUS_216.78"))});
        auto env = QProcessEnvironment::systemEnvironment();
        // [deploy] Anchor the runner's savedata/assets/fonts (and settings.toml)
        // at the deploy root -- where the launcher wrote them -- not data/.
        env.insert(QStringLiteral("PS2X_EXEDIR"), apppaths::userRoot());
        env.insert(QStringLiteral("PS2X_ASSETDIR"), apppaths::assets());
        // [logfix] The runner has no console here, so it only writes logs/bt3.log when asked to
        // (its own redirect is for a console stderr). Honour the Logging tab's level, and a
        // PS2X_LOGFILE the user already exported.
        if (SettingsManager::instance().logLevel() > 0 && !env.contains(QStringLiteral("PS2X_LOGFILE")))
        {
            const QString logsDir = QDir(apppaths::userRoot()).filePath(QStringLiteral("logs"));
            QDir().mkpath(logsDir);
            const QString logPath = QDir(logsDir).filePath(QStringLiteral("bt3.log"));
            const QString prevPath = QDir(logsDir).filePath(QStringLiteral("bt3.prev.log"));
            QFile::remove(prevPath);             // keep the previous run's log, as the runner did
            QFile::rename(logPath, prevPath);
            env.insert(QStringLiteral("PS2X_LOGFILE"), logPath);
        }
#ifdef _WIN32
        // The bundle lives in assets/lib now (flat copies also sit next to the exes); prepend it to
        // PATH so the runner resolves its DLLs even when a file was not flattened.
        {
            const QString libDir = QDir(apppaths::assets()).filePath(QStringLiteral("lib"));
            const QString path = env.value(QStringLiteral("PATH"));
            env.insert(QStringLiteral("PATH"), libDir + QLatin1Char(';') + path);
        }
        // [vulkan] Windows: paraLLEl-GS runs on the bundled Mesa lavapipe ICD by
        // default. The vendor Vulkan driver (AMD amdvlk64.dll) access-violates
        // inside its shader compiler on Polaris/GCN parts and kills the runner.
        // Set PS2X_VK_NATIVE=1 to opt out and use the system Vulkan driver.
        if ((SettingsManager::instance().renderer() == SettingsManager::kRendererParallelGS ||
             SettingsManager::instance().renderer() == SettingsManager::kRendererNative) &&
            qEnvironmentVariable("PS2X_VK_NATIVE") != QLatin1String("1"))
        {
            const QString lvp = QDir(apppaths::assets()).filePath(QStringLiteral("lavapipe/lvp_icd.x86_64.json"));
            if (QFile::exists(lvp))
            {
                env.insert(QStringLiteral("VK_DRIVER_FILES"), lvp);
                env.insert(QStringLiteral("VK_ICD_FILENAMES"), lvp);
            }
        }
#endif
#if !defined(_WIN32) && !defined(Q_OS_MACOS)
        // position-independent loader search is a POSIX concept; Windows
        // resolves the bundled dlls from the executable's own directory, and
        // the macOS bundle resolves its dylibs through @rpath.
        env.insert(QStringLiteral("LD_LIBRARY_PATH"),
                   QDir(apppaths::assets()).filePath(QStringLiteral("lib")));
#endif
        proc->setProcessEnvironment(env);
    }
    else
    {
        // Launch detached: the game extracts + execs its own inner runner.
        proc->setProgram(m_gameElf);
    }
#ifdef _WIN32
    if (m_plainRunner)
    {
        // [vulkan] Keep the game under the launcher so a crash in the vendor
        // Vulkan driver (or any early failure) is detected and retried once with
        // OpenGL, dropping a line in logs/vulkan-fallback.log.
        m_gameProc = proc;
        const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
        connect(proc, &QProcess::finished, this,
                [this, proc, startMs](int code, QProcess::ExitStatus status)
                {
                    const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - startMs;
                    const bool crashed = (status == QProcess::CrashExit) ||
                                         (code != 0 && (static_cast<quint32>(code) & 0xC0000000u) != 0u);
                    const bool retryable =
                        crashed && elapsedMs < 60000 && !m_fallbackRetried &&
                        (SettingsManager::instance().renderer() == SettingsManager::kRendererParallelGS ||
                         SettingsManager::instance().renderer() == SettingsManager::kRendererNative);
                    if (retryable)
                    {
                        m_fallbackRetried = true;
                        logVulkanFallback(QStringLiteral(
                            "runner exited abnormally (code=%1 status=%2 after %3 ms); "
                            "retrying with the OpenGL renderer")
                            .arg(code).arg(int(status)).arg(elapsedMs));
                        SettingsManager::instance().setRenderer(SettingsManager::kRendererOpenGL);
                        SettingsManager::instance().save();
                        m_gameProc = nullptr;
                        proc->deleteLater();
                        onPlayClicked();
                        return;
                    }
                    m_gameProc = nullptr;
                    proc->deleteLater();
                    qApp->quit();
                });
        proc->start();
        if (!proc->waitForStarted(5000))
        {
            QMessageBox::critical(this, QStringLiteral("Could not start the game"), proc->errorString());
            proc->deleteLater();
            m_gameProc = nullptr;
            return;
        }
        // Hide (do not close) so the launcher survives to relay the fallback.
        hide();
        return;
    }
#endif

    if (!proc->startDetached())
    {
        QMessageBox::critical(this, QStringLiteral("Could not start the game"), proc->errorString());
        proc->deleteLater();
        return;
    }

    // The launcher's job is done: close this window (the game runs on its own).
    close();
}

void LauncherWindow::checkGameData()
{
    resolveLaunchTarget();
    m_gameDataValid = (DiscVerify::verifyInstalledData(m_dataDir) == DiscVerify::State::Valid);

    if (m_play)
    {
        const bool enabled = (!m_gameElf.isEmpty() || m_plainRunner) && m_gameDataValid;
        m_play->setEnabled(enabled);
        setButtonGlow(m_play, m_playGlow, m_playGlowAnim, enabled);   // [glow] PLAY pulses while enabled
    }

    updateHint();
}

void LauncherWindow::updateHint()
{
    const QString kRed = QStringLiteral("#ef4444");
    const QString kGreen = QStringLiteral("#22c55e");
    const QString kDim = QStringLiteral("#9999b3");

    QString color;
    QString text;
    if (!m_gameDataValid)
    {
        color = kRed;
        text = QStringLiteral("Missing or Corrupted Data");
    }
    else if (!m_gameElf.isEmpty() || m_plainRunner)
    {
        color = kGreen;
        text = QStringLiteral("Ready to Play");
    }
    else
    {
        color = kDim;
        text = QStringLiteral("No self-extracting game ELF found in this folder");
    }
    if (m_hint)
        m_hint->setText(QStringLiteral("<span style=\"color:%1; font-size:15px;\">●</span> %2")
                            .arg(color, text));
}

void LauncherWindow::openInstallWizard()
{
    auto *view = new InstallWizardView(this, /*reinstall=*/false);
    view->onFinished = [this, view](bool accepted) {
        const bool openSettings = view->openSettingsRequested();
        const bool play = view->playRequested();
        view->deleteLater();
        checkGameData();
        showLauncher();
        if (accepted && openSettings)
            onSettingsClicked();
        else if (accepted && play && m_gameDataValid)
            onPlayClicked();
    };
    showView(view);
}

void LauncherWindow::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);

    // Pop the install wizard automatically on first launch when the game data
    // is missing/corrupt. The subsequent runs are user-initiated (PLAY button).
    if (!m_gameDataValid && !m_wizardShown)
    {
        m_wizardShown = true;
        QTimer::singleShot(0, this, [this] {
            if (!m_gameDataValid)
                openInstallWizard();
        });
    }
}

void LauncherWindow::onSettingsClicked()
{
    stopSettingsGlow();
    auto *view = new SettingsView(this);
    view->onBack = [this] { popView(); };
    showView(view);
}

void LauncherWindow::showView(QWidget *view)
{
    if (!m_stack || !view)
        return;
    if (m_stack->indexOf(view) < 0)
        m_stack->addWidget(view);
    m_stack->setCurrentWidget(view);
    // [fade] Subtle fade-in when a view takes over the window.
    auto *fx = new QGraphicsOpacityEffect(view);
    view->setGraphicsEffect(fx);
    auto *anim = new QPropertyAnimation(fx, "opacity", view);
    anim->setDuration(140);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QPropertyAnimation::finished, view, [view] { view->setGraphicsEffect(nullptr); });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
    update();
}

void LauncherWindow::showLauncher()
{
    if (m_stack)
        m_stack->setCurrentIndex(0);
    checkGameData();
    update();
}

void LauncherWindow::popView()
{
    if (!m_stack || m_stack->count() <= 1)
        return;
    QWidget *cur = m_stack->currentWidget();
    if (!cur || cur == m_stack->widget(0))
        return;
    m_stack->removeWidget(cur);
    cur->deleteLater();   // the view owns no external refs
    m_stack->setCurrentIndex(m_stack->count() - 1);
    update();
}

void LauncherWindow::startSettingsGlow()
{
    setButtonGlow(m_settings, m_settingsGlow, m_settingsGlowAnim, true);
}

void LauncherWindow::stopSettingsGlow()
{
    setButtonGlow(m_settings, m_settingsGlow, m_settingsGlowAnim, false);
}

void LauncherWindow::setButtonGlow(QPushButton *btn, QGraphicsDropShadowEffect *&fx,
                                   QPropertyAnimation *&anim, bool on)
{
    if (!btn)
        return;

    if (on)
    {
        if (fx)
            return;   // already glowing
        fx = new QGraphicsDropShadowEffect(btn);
        fx->setOffset(0, 0);
        fx->setBlurRadius(6.0);
        fx->setColor(QColor(255, 158, 26, 210));   // dbz::kAccent halo
        btn->setGraphicsEffect(fx);

        anim = new QPropertyAnimation(fx, "blurRadius", this);
        anim->setDuration(1200);
        anim->setLoopCount(-1);
        anim->setEasingCurve(QEasingCurve::InOutSine);
        anim->setKeyValueAt(0.0, 6.0);
        anim->setKeyValueAt(0.5, 30.0);
        anim->setKeyValueAt(1.0, 6.0);
        anim->start();
    }
    else
    {
        if (anim)
        {
            anim->stop();
            delete anim;
            anim = nullptr;
        }
        if (fx)
        {
            // setGraphicsEffect(nullptr) makes QWidget delete the previous effect.
            btn->setGraphicsEffect(nullptr);
            fx = nullptr;
        }
    }
}

void LauncherWindow::loadBackground() { /* bg applied in paintEvent */ }

void LauncherWindow::paintEvent(QPaintEvent *)
{
    // [inwindow] A view (settings/wizard/...) is showing: it paints its own opaque
    // background, so skip the launcher background.
    if (m_stack && m_stack->currentIndex() != 0)
        return;
    // The background image only fills the area above the (opaque) bottom bar.
    const int barY = m_bottomBar ? m_bottomBar->y() : height();
    const QRect bgArea(0, 0, width(), barY);

    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!m_bgPath.isEmpty())
    {
        QPixmap bg(m_bgPath);
        if (!bg.isNull())
        {
            p.drawPixmap(bgArea, bg.scaled(bgArea.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            return;
        }
    }
    // Fallback: dark DBZ gradient (top->bottom) within the image area.
    QLinearGradient g(0, 0, 0, bgArea.height());
    g.setColorAt(0.0, QColor(16, 22, 28));
    g.setColorAt(0.5, QColor(9, 14, 18));
    g.setColorAt(1.0, QColor(5, 8, 12));
    p.fillRect(bgArea, g);

    // Corner brackets (accent orange), matching the capsule-HUD look.
    const QColor bracket(255, 158, 26, 200);
    QPen pen(bracket, 2.0);
    p.setPen(pen);
    const int bl = 14, off = 6;
    const QRect r = bgArea.adjusted(1, 1, -1, -1);
    QPoint tl = r.topLeft(), tr = r.topRight(), blc = r.bottomLeft(), br = r.bottomRight();
    p.drawLine(tl + QPoint(off, off), tl + QPoint(off + bl, off));
    p.drawLine(tl + QPoint(off, off), tl + QPoint(off, off + bl));
    p.drawLine(tr - QPoint(off, off), tr - QPoint(off + bl, -off));
    p.drawLine(tr - QPoint(off, off), tr - QPoint(-off, off + bl));
    p.drawLine(blc + QPoint(off, -off), blc + QPoint(off + bl, -off));
    p.drawLine(blc + QPoint(off, -off), blc + QPoint(off, -off - bl));
    p.drawLine(br - QPoint(off, -off), br - QPoint(off + bl, -off));
    p.drawLine(br - QPoint(off, -off), br - QPoint(-off, off + bl));
}
