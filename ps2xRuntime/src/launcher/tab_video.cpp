#include "tab_video.h"

#include "dbz_theme.h"
#include "recommendation.h"
#include "settings_manager.h"
#include "tex_install_dialog.h"   // [texreplace] pack install dialog (moved from Misc)
#include "tex_pack.h"   // [texui] pack status for the Texture Replacement dialog
#include "view_host.h"  // [inwindow] push nested views

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace
{
    QLabel *sectionLabel(const QString &text)
    {
        auto *l = new QLabel(text);
        l->setObjectName(QStringLiteral("sectionLabel"));
        return l;
    }

    QLabel *hintRow(const QString &text)
    {
        auto *l = new QLabel(text);
        l->setObjectName(QStringLiteral("hintLabel"));
        l->setWordWrap(true);
        return l;
    }

    QLabel *valueLabel(const QString &text = QString())
    {
        auto *l = new QLabel(text);
        l->setObjectName(QStringLiteral("valueLabel"));
        return l;
    }

    QWidget *sliderPair(const QString &label, QSlider **sl, QLabel **val,
                        int lo, int hi, int cur, const char *fmt = "%d")
    {
        auto *row = new QWidget;
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(8, 2, 8, 2);
        lay->setSpacing(10);
        auto *lbl = valueLabel(label);
        lbl->setMinimumWidth(110);
        *sl = new QSlider(Qt::Horizontal);
        (*sl)->setRange(lo, hi);
        (*sl)->setValue(cur);
        *val = valueLabel(QString::asprintf(fmt, cur));
        (*val)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        (*val)->setMinimumWidth(56);
        lay->addWidget(lbl);
        lay->addWidget(*sl, 1);
        lay->addWidget(*val);
        return row;
    }

    QWidget *toggleRow(const QString &label, QCheckBox **cb, bool checked,
                       const QString &tooltip = QString())
    {
        auto *row = new QWidget;
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(8, 2, 8, 2);
        auto *lbl = valueLabel(label);
        *cb = new QCheckBox;
        (*cb)->setChecked(checked);
        if (!tooltip.isEmpty())
        {
            lbl->setToolTip(tooltip);
            (*cb)->setToolTip(tooltip);
        }
        lay->addWidget(lbl, 1);
        lay->addWidget(*cb);
        return row;
    }

    QWidget *comboRow(const QString &label, QComboBox **combo, const QStringList &items, int cur)
    {
        auto *row = new QWidget;
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(8, 2, 8, 2);
        lay->setSpacing(10);
        auto *lbl = valueLabel(label);
        lbl->setMinimumWidth(110);
        *combo = new QComboBox;
        (*combo)->addItems(items);
        (*combo)->setCurrentIndex(cur);
        (*combo)->setMinimumWidth(220);
        lay->addWidget(lbl);
        lay->addWidget(*combo, 1);
        return row;
    }

    // Window-size presets shared with the in-game overlay.
    constexpr int kWinW[] = {1024, 1280, 1360, 1366, 1440, 1600, 1920, 2560, 3440, 3840};
    constexpr int kWinH[] = {768, 720, 768, 768, 900, 900, 1080, 1440, 1440, 2160};
    constexpr int kWinCount = 10;

    constexpr const char *kDotColor[3] = {"#3fba4f", "#d1991f", "#f75245"};   // ok / fallback / fail

    void paintDot(QLabel *l, int state)
    {
        l->setStyleSheet(QStringLiteral("color:%1;font-weight:bold;")
                             .arg(QString::fromLatin1(kDotColor[std::clamp(state, 0, 2)])));
    }

    QString monitorValue(int idx)
    {
        const auto screens = QGuiApplication::screens();
        if (idx < 0 || idx >= screens.size() || !screens[idx])
            return QStringLiteral("%1 - ?").arg(idx + 1);
        const QScreen *sc = screens[idx];
        const QString name = sc->model().isEmpty() ? sc->name() : sc->model();
        return QStringLiteral("%1 - %2").arg(idx + 1).arg(name.isEmpty() ? QStringLiteral("?") : name);
    }

    QString monitorNote(int idx)
    {
        const auto screens = QGuiApplication::screens();
        if (idx < 0 || idx >= screens.size() || !screens[idx])
            return QStringLiteral("not detected");
        const QScreen *sc = screens[idx];
        const QRect g = sc->geometry();
        return QStringLiteral("%1x%2 @%3Hz")
            .arg(g.width()).arg(g.height()).arg(qRound(sc->refreshRate()));
    }

    QStringList monitorItems()
    {
        QStringList items;
        const auto screens = QGuiApplication::screens();
        for (int i = 0; i < screens.size(); ++i)
            items << QStringLiteral("%1 - %2 - %3").arg(monitorValue(i)).arg(monitorNote(i));
        if (items.isEmpty()) items << QStringLiteral("1 - Primary");
        return items;
    }

    // [inwindow] Shared shell for the Video-tab settings views (no longer popups): Apply =
    // write through to SettingsManager (in memory), Save = + persist, Reset = back to the
    // values it opened with, Back = return to the previous page.
    class VideoView : public QWidget
    {
    public:
        explicit VideoView(QWidget *parent) : QWidget(parent)
        {
            setAttribute(Qt::WA_StyledBackground, true);
            setStyleSheet(QStringLiteral("VideoView { background-color: #0a1014; }"));
            setMinimumWidth(520);
        }

        std::function<void()> onApplied;
        std::function<void()> onBack;

    protected:
        virtual void loadOpened() = 0;       // snapshot -> widgets
        virtual void writeThrough() = 0;     // widgets -> SettingsManager
        virtual void snapshot() = 0;         // widgets -> snapshot

        QHBoxLayout *buttonRow()
        {
            auto *row = new QHBoxLayout;
            auto *reset = new QPushButton(QStringLiteral("Reset"));
            auto *back = new QPushButton(QStringLiteral("Back"));
            auto *apply = new QPushButton(QStringLiteral("Apply"));
            auto *save = new QPushButton(QStringLiteral("Save"));
            for (auto *b : {reset, back, apply, save}) b->setMinimumWidth(96);
            connect(reset, &QPushButton::clicked, this, [this] { loadOpened(); });
            connect(back, &QPushButton::clicked, this, [this] { if (onBack) onBack(); });
            connect(apply, &QPushButton::clicked, this, [this] {
                writeThrough();
                if (onApplied) onApplied();
            });
            connect(save, &QPushButton::clicked, this, [this] {
                writeThrough();
                SettingsManager::instance().save();
                if (onApplied) onApplied();
                if (onBack) onBack();
            });
            row->addWidget(reset);
            row->addWidget(back);
            row->addStretch(1);
            row->addWidget(apply);
            row->addWidget(save);
            return row;
        }
    };

    // ---------------------------------------------------------------------------------------------
    // Display settings: resolution, render scale, monitor, window mode.
    // ---------------------------------------------------------------------------------------------
    class DisplayView : public VideoView
    {
    public:
        explicit DisplayView(QWidget *parent) : VideoView(parent)
        {
            auto &s = SettingsManager::instance();
            auto *root = new QVBoxLayout(this);

            root->addWidget(sectionLabel(QStringLiteral("RESOLUTION")));
            m_res = new QComboBox;
            for (int i = 0; i < kWinCount; ++i)
            {
                m_res->addItem(QStringLiteral("%1 x %2").arg(kWinW[i]).arg(kWinH[i]));
                m_resW << kWinW[i];
                m_resH << kWinH[i];
            }
            {
                QString custom = QStringLiteral("%1 x %2 (custom)").arg(s.windowW()).arg(s.windowH());
                int found = -1;
                for (int i = 0; i < kWinCount; ++i)
                    if (kWinW[i] == s.windowW() && kWinH[i] == s.windowH()) { found = i; break; }
                if (found < 0 && s.windowW() > 0 && s.windowH() > 0)
                {
                    m_res->addItem(custom);
                    m_resW << s.windowW();
                    m_resH << s.windowH();
                }
            }
            root->addWidget(m_res);
            root->addWidget(hintRow(QStringLiteral("The projection FOV follows the window aspect, so a wider "
                                                   "window genuinely shows more stage. Windowed mode only.")));

            root->addWidget(sectionLabel(QStringLiteral("RENDER SCALE")));
            {
                auto *row = new QHBoxLayout;
                auto *box = new QWidget;
                auto *bl = new QHBoxLayout(box);
                bl->setContentsMargins(8, 2, 8, 2);
                m_x1 = new QRadioButton(QStringLiteral("x1"));
                m_x2 = new QRadioButton(QStringLiteral("x2"));
                m_x3 = new QRadioButton(QStringLiteral("x3"));
                bl->addWidget(m_x1);
                bl->addWidget(m_x2);
                bl->addWidget(m_x3);
                bl->addStretch(1);
                row->addWidget(box, 1);
                root->addLayout(row);
            }
            root->addWidget(hintRow(QStringLiteral("paraLLEl-GS: 1x / 2x / 3x = 1 / 4 / 8 samples per pixel, "
                                                   "applies live. OpenGL (New) applies on restart.")));

            root->addWidget(sectionLabel(QStringLiteral("MONITOR")));
            m_mon = new QComboBox;
            m_mon->addItems(monitorItems());
            root->addWidget(m_mon);

            root->addWidget(sectionLabel(QStringLiteral("WINDOW MODE")));
            {
                auto *box = new QWidget;
                auto *bl = new QHBoxLayout(box);
                bl->setContentsMargins(8, 2, 8, 2);
                m_win = new QRadioButton(QStringLiteral("Windowed (resizable)"));
                m_borderless = new QRadioButton(QStringLiteral("Borderless"));
                m_full = new QRadioButton(QStringLiteral("Fullscreen"));
                bl->addWidget(m_win);
                bl->addWidget(m_borderless);
                bl->addWidget(m_full);
                bl->addStretch(1);
                root->addWidget(box);
            }
            root->addWidget(hintRow(QStringLiteral("Windowed and borderless use the resolution above; fullscreen "
                                                   "uses the monitor's current mode.")));

            root->addLayout(buttonRow());
            // Preselect from the saved settings: without this the widgets keep their construction
            // defaults (monitor index 0, first resolution, no radio checked) and the dialog showed --
            // and Saved -- the wrong values, so a chosen monitor never appeared to stick.
            {
                int res = 0;
                for (int i = 0; i < kWinCount; ++i)
                    if (kWinW[i] == s.windowW() && kWinH[i] == s.windowH()) { res = i; break; }
                m_res->setCurrentIndex(res);
                const int scale = std::clamp(s.renderScale(), 1, 3);
                (scale >= 3 ? m_x3 : scale == 2 ? m_x2 : m_x1)->setChecked(true);
                m_mon->setCurrentIndex(std::clamp(s.monitor(), 0, std::max(0, m_mon->count() - 1)));
                const int mode = s.windowMode();
                (mode == 2 ? m_full : mode == 1 ? m_borderless : m_win)->setChecked(true);
            }
            m_opened = capture();
            loadOpened();
        }

    private:
        struct Snap
        {
            int res = 0, scale = 1, mon = 0, mode = 0;
        };

        Snap capture() const
        {
            Snap v;
            v.res = m_res->currentIndex();
            v.scale = m_x1->isChecked() ? 1 : m_x2->isChecked() ? 2 : 3;
            v.mon = m_mon->currentIndex();
            v.mode = m_win->isChecked() ? 0 : m_borderless->isChecked() ? 1 : 2;
            return v;
        }

        void loadOpened() override
        {
            m_res->setCurrentIndex(std::clamp(m_opened.res, 0, m_res->count() - 1));
            m_mon->setCurrentIndex(std::clamp(m_opened.mon, 0, m_mon->count() - 1));
            (m_opened.scale >= 3 ? m_x3 : m_opened.scale == 2 ? m_x2 : m_x1)->setChecked(true);
            (m_opened.mode == 2 ? m_full : m_opened.mode == 1 ? m_borderless : m_win)->setChecked(true);
        }

        void snapshot() override { m_opened = capture(); }
        void writeThrough() override
        {
            auto &s = SettingsManager::instance();
            const Snap v = capture();
            if (v.res >= 0 && v.res < m_resW.size()) s.setWindowSize(m_resW[v.res], m_resH[v.res]);
            s.setMonitor(v.mon);
            s.setWindowMode(v.mode);
            s.setFullscreen(v.mode == 2);
            s.setRenderScale(std::clamp(v.scale, 1, 3));
        }

        QComboBox *m_res = nullptr, *m_mon = nullptr;
        QRadioButton *m_x1 = nullptr, *m_x2 = nullptr, *m_x3 = nullptr;
        QRadioButton *m_win = nullptr, *m_borderless = nullptr, *m_full = nullptr;
        QList<int> m_resW, m_resH;
        Snap m_opened;
    };

    // ---------------------------------------------------------------------------------------------
    // Visual effects: the renderer effects (same names as the overlay) + the filtering toggles.
    // ---------------------------------------------------------------------------------------------
    class EffectsView : public VideoView
    {
    public:
        explicit EffectsView(QWidget *parent) : VideoView(parent)
        {
            auto &s = SettingsManager::instance();
            auto *root = new QVBoxLayout(this);

            root->addWidget(sectionLabel(QStringLiteral("EFFECTS")));
            root->addWidget(toggleRow(QStringLiteral("Cel Outline"), &m_outline, s.outline()));
            m_inkRow = sliderPair(QStringLiteral("Ink Strength"), &m_ink, &m_inkVal, 100, 260,
                                  s.inkStrength(), "%d %");
            root->addWidget(m_inkRow);
            root->addWidget(hintRow(QStringLiteral("199% matches the console line. Lower = thinner/lighter ink.")));
            root->addWidget(toggleRow(QStringLiteral("Character Shadows"), &m_shadows, s.shadows()));
            root->addWidget(toggleRow(QStringLiteral("Depth-of-Field Blur"), &m_dof, s.dofBlur()));
            m_dofRow = sliderPair(QStringLiteral("Blur Reach"), &m_dofReach, &m_dofVal, 50, 400,
                                  s.dofZFar() / 1000, "%d k");
            root->addWidget(m_dofRow);
            root->addWidget(hintRow(QStringLiteral("Lower = blur reaches nearer to the camera. 200k matches the "
                                                   "console look.")));
            root->addWidget(toggleRow(QStringLiteral("Glow (Kaioken aura)"), &m_glow, s.glow(),
                                      QStringLiteral("Character/attack bloom. Applies on restart.")));

            root->addWidget(sectionLabel(QStringLiteral("FILTERING")));
            root->addWidget(toggleRow(QStringLiteral("Bilinear Filter"), &m_bilinear, s.bilinear()));
            root->addWidget(toggleRow(QStringLiteral("Force Filtering (smooth terrain)"), &m_forceBilinear,
                                      s.forceBilinear()));

            root->addLayout(buttonRow());

            connect(m_outline, &QCheckBox::toggled, this, [this](bool on) { m_inkRow->setVisible(on); });
            connect(m_dof, &QCheckBox::toggled, this, [this](bool on) { m_dofRow->setVisible(on); });
            connect(m_ink, &QSlider::valueChanged, this, [this](int v) {
                m_inkVal->setText(QString::number(v) + QStringLiteral(" %"));
            });
            connect(m_dofReach, &QSlider::valueChanged, this, [this](int v) {
                m_dofVal->setText(QString::number(v) + QStringLiteral(" k"));
            });

            m_opened = capture();
            loadOpened();
        }

    private:
        struct Snap
        {
            bool outline = false, shadows = true, dof = false, glow = true, bilinear = true, force = true;
            int ink = 199, reach = 200;
        };

        Snap capture() const
        {
            Snap v;
            v.outline = m_outline->isChecked();
            v.shadows = m_shadows->isChecked();
            v.dof = m_dof->isChecked();
            v.glow = m_glow->isChecked();
            v.bilinear = m_bilinear->isChecked();
            v.force = m_forceBilinear->isChecked();
            v.ink = m_ink->value();
            v.reach = m_dofReach->value();
            return v;
        }

        void loadOpened() override
        {
            m_outline->setChecked(m_opened.outline);
            m_shadows->setChecked(m_opened.shadows);
            m_dof->setChecked(m_opened.dof);
            m_glow->setChecked(m_opened.glow);
            m_bilinear->setChecked(m_opened.bilinear);
            m_forceBilinear->setChecked(m_opened.force);
            m_ink->setValue(m_opened.ink);
            m_dofReach->setValue(m_opened.reach);
            m_inkRow->setVisible(m_opened.outline);
            m_dofRow->setVisible(m_opened.dof);
        }

        void snapshot() override { m_opened = capture(); }
        void writeThrough() override
        {
            auto &s = SettingsManager::instance();
            const Snap v = capture();
            s.setOutline(v.outline);
            s.setInkStrength(v.ink);
            s.setShadows(v.shadows);
            s.setDofBlur(v.dof);
            s.setDofZFar(v.reach * 1000);
            s.setGlow(v.glow);
            s.setBilinear(v.bilinear);
            s.setForceBilinear(v.force);
        }

        QCheckBox *m_outline = nullptr, *m_shadows = nullptr, *m_dof = nullptr, *m_glow = nullptr;
        QCheckBox *m_bilinear = nullptr, *m_forceBilinear = nullptr;
        QWidget *m_inkRow = nullptr, *m_dofRow = nullptr;
        QSlider *m_ink = nullptr, *m_dofReach = nullptr;
        QLabel *m_inkVal = nullptr, *m_dofVal = nullptr;
        Snap m_opened;
    };

    // ---------------------------------------------------------------------------------------------
    // [texui] Texture Replacement: pack status + enable + install + the pack options. The whole
    // texture-pack flow lives here now (moved out of Misc).
    // ---------------------------------------------------------------------------------------------
    class TexPackView : public VideoView
    {
    public:
        explicit TexPackView(QWidget *parent) : VideoView(parent)
        {
            auto &s = SettingsManager::instance();
            auto *root = new QVBoxLayout(this);

            root->addWidget(sectionLabel(QStringLiteral("PACK STATUS")));
            {
                auto *row = new QWidget;
                auto *lay = new QHBoxLayout(row);
                lay->setContentsMargins(8, 2, 8, 2);
                lay->setSpacing(8);
                m_statusDot = new QLabel(QStringLiteral("*"));
                m_statusDot->setFixedWidth(12);
                m_statusDot->setStyleSheet(QStringLiteral("font-weight:bold;"));
                m_statusTxt = valueLabel();
                lay->addWidget(m_statusDot);
                lay->addWidget(m_statusTxt, 1);
                root->addWidget(row);
            }
            m_statusNote = hintRow(QString());
            root->addWidget(m_statusNote);
            root->addWidget(hintRow(texpack::dir()));

            root->addWidget(sectionLabel(QStringLiteral("PACK")));
            root->addWidget(toggleRow(QStringLiteral("Enable Texture Replacement"), &m_enable, s.texPack(),
                                      QStringLiteral("Use the installed pack (shares the key with the in-game overlay).")));
            {
                auto *row = new QHBoxLayout;
                m_install = new QPushButton(QStringLiteral("Install texture pack..."));
                auto *folder = new QPushButton(QStringLiteral("Open folder"));
                m_install->setObjectName(QStringLiteral("wizardButton"));
                folder->setObjectName(QStringLiteral("wizardButton"));
                m_install->setCursor(Qt::PointingHandCursor);
                folder->setCursor(Qt::PointingHandCursor);
                connect(m_install, &QPushButton::clicked, this, [this] { openInstall(); });
                connect(folder, &QPushButton::clicked, this, [this] {
                    const QString d = texpack::dir();
                    if (QDir().mkpath(d)) QDesktopServices::openUrl(QUrl::fromLocalFile(d));
                });
                row->addWidget(m_install);
                row->addWidget(folder);
                row->addStretch(1);
                root->addLayout(row);
            }

            root->addWidget(sectionLabel(QStringLiteral("OPTIONS")));
            m_optsBox = new QWidget;
            auto *bl = new QVBoxLayout(m_optsBox);
            bl->setContentsMargins(0, 0, 0, 0);
            bl->addWidget(toggleRow(QStringLiteral("Video overlay (4K intro)"), &m_intro, s.introVideo()));
            bl->addWidget(comboRow(QStringLiteral("Buttons style"), &m_buttons,
                                   {QStringLiteral("PS2"), QStringLiteral("Xbox")}, s.buttonLayout()));
            root->addWidget(m_optsBox);
            root->addWidget(hintRow(QStringLiteral(
                "Video overlay replaces the opening movie with the pack's 4K clip. Both options "
                "apply on restart.")));

            root->addLayout(buttonRow());

            refreshStatus();
            m_opened = capture();
            loadOpened();
        }

    private:
        struct Snap { bool enable = false, intro = true; int buttons = 1; };

        void refreshStatus()
        {
            const quint64 n = texpack::countReplacements();
            const bool installed = n > 0;
            m_statusDot->setStyleSheet(QStringLiteral("font-weight:bold; color:%1;")
                                           .arg(installed ? QStringLiteral("#22c55e") : QStringLiteral("#ef4444")));
            m_statusTxt->setText(installed
                                     ? QStringLiteral("Installed - %1 replacements").arg(n)
                                     : QStringLiteral("No texture pack indexed"));
            if (installed)
            {
                const bool full = QDir(texpack::dir() + QStringLiteral("/replacements/Characters/Body")).exists();
                m_statusNote->setText(full ? QStringLiteral("Full pack: 3D + 2D")
                                           : QStringLiteral("Lite pack: 2D only"));
            }
            else
                m_statusNote->setText(QStringLiteral(
                    "Install one below, or point PS2X_TEXREPLACE=<dir> at a pack."));
            m_optsBox->setEnabled(installed);
        }

        void openInstall()
        {
            QMenu menu(this);
            QAction *lite = menu.addAction(QStringLiteral("Pack Lite (2D only)"));
            menu.addAction(QStringLiteral("Pack Full (3D + 2D)"));
            QAction *chosen = menu.exec(m_install->mapToGlobal(QPoint(0, m_install->height())));
            if (!chosen)
                return;
            const int kind = (chosen == lite) ? texpack::kPackLite : texpack::kPackFull;
            auto *view = new TexInstallView(this, kind);
            connect(view, &TexInstallView::installed, this, [this] { refreshStatus(); });
            view->onBack = [] { viewhost::pop(); };
            viewhost::show(view);
        }

        Snap capture() const { return { m_enable->isChecked(), m_intro->isChecked(), m_buttons->currentIndex() }; }
        void loadOpened() override
        {
            m_enable->setChecked(m_opened.enable);
            m_intro->setChecked(m_opened.intro);
            m_buttons->setCurrentIndex(m_opened.buttons);
        }
        void snapshot() override { m_opened = capture(); }
        void writeThrough() override
        {
            auto &s = SettingsManager::instance();
            const Snap v = capture();
            s.setTexPack(v.enable);
            s.setIntroVideo(v.intro);
            s.setButtonLayout(v.buttons);
        }

        QLabel *m_statusDot = nullptr, *m_statusTxt = nullptr, *m_statusNote = nullptr;
        QCheckBox *m_enable = nullptr, *m_intro = nullptr;
        QComboBox *m_buttons = nullptr;
        QPushButton *m_install = nullptr;
        QWidget *m_optsBox = nullptr;
        Snap m_opened;
    };
} // namespace

VideoTab::VideoTab(QWidget *parent)
    : QWidget(parent)
{
    auto &s = SettingsManager::instance();

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"
                                         "QScrollArea > QWidget > QWidget { background: transparent; }"));

    auto *content = new QWidget;
    auto *root = new QVBoxLayout(content);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(6);

    // STATUS: what the launcher will ask the runtime to run. Green = as configured, amber = downgraded
    // (clamped monitor, restart needed), red = unavailable.
    root->addWidget(sectionLabel(QStringLiteral("STATUS")));
    {
        auto *grid = new QGridLayout;
        grid->setContentsMargins(8, 4, 8, 4);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(4);
        static const char *const kNames[4] = {"Renderer", "Monitor", "Resolution", "Upscale"};
        for (int r = 0; r < 4; ++r)
        {
            m_dot[r] = new QLabel(QStringLiteral("*"));
            m_dot[r]->setFixedWidth(12);
            auto *nm = valueLabel(QString::fromLatin1(kNames[r]));
            nm->setMinimumWidth(90);
            m_val[r] = valueLabel();
            m_val[r]->setMinimumWidth(190);
            m_note[r] = hintRow(QString());
            grid->addWidget(m_dot[r], r, 0);
            grid->addWidget(nm, r, 1);
            grid->addWidget(m_val[r], r, 2);
            grid->addWidget(m_note[r], r, 3);
        }
        grid->setColumnStretch(3, 1);
        root->addLayout(grid);
    }

    // The two dialogs, same shape as the in-game overlay.
    {
        auto *row = new QHBoxLayout;
        auto *disp = new QPushButton(QStringLiteral("Display settings..."));
        auto *fx = new QPushButton(QStringLiteral("Visual Effects..."));
        auto *tex = new QPushButton(QStringLiteral("Texture Replacement..."));
        disp->setMinimumWidth(200);
        fx->setMinimumWidth(200);
        tex->setMinimumWidth(200);
        connect(disp, &QPushButton::clicked, this, &VideoTab::openDisplayDialog);
        connect(fx, &QPushButton::clicked, this, &VideoTab::openVisualEffectsDialog);
        connect(tex, &QPushButton::clicked, this, &VideoTab::openTexPackDialog);
        row->addWidget(disp);
        row->addWidget(fx);
        row->addWidget(tex);
        row->addStretch(1);
        root->addLayout(row);
    }

    // RENDERER (stays on the tab: it is the one switch that changes everything else)
    root->addWidget(sectionLabel(QStringLiteral("RENDERER")));
    QStringList renderers = {QStringLiteral("OpenGL (New)"), QStringLiteral("Software (CPU)"),
                             QStringLiteral("paraLLEl-GS (Vulkan)"), QStringLiteral("Native Vulkan (engine seam)")};
    const QList<int> rendererValues = {SettingsManager::kRendererOpenGL,
                                       SettingsManager::kRendererSoftware,
                                       SettingsManager::kRendererParallelGS,
                                       SettingsManager::kRendererNative};   // [nativeopt]
    int curRenderer = 0;
    for (int i = 0; i < rendererValues.size(); ++i)
        if (rendererValues[i] == s.renderer()) { curRenderer = i; break; }
    root->addWidget(comboRow(QStringLiteral("Renderer"), &m_renderer, renderers, curRenderer));
    for (int i = 0; i < rendererValues.size(); ++i)
        m_renderer->setItemData(i, rendererValues[i]);
    root->addWidget(hintRow(QStringLiteral(
        "paraLLEl-GS is the default backend (Vulkan compute; falls back to OpenGL (New) if Vulkan is "
        "unavailable). OpenGL (New) presents through our own GL layer; Software uses the CPU rasterizer "
        "and has no upscale.")));
    root->addWidget(hintRow(QStringLiteral("Takes full effect after restart.")));

    // [tier] RECOMMENDED: Detect (probe + benchmark) then Apply (write the ceiling).
    root->addWidget(sectionLabel(QStringLiteral("RECOMMENDED")));
    {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(8, 2, 8, 2);
        auto *detectBtn = new QPushButton(QStringLiteral("Detect"));
        detectBtn->setObjectName(QStringLiteral("wizardButton"));
        detectBtn->setCursor(Qt::PointingHandCursor);
        auto *applyBtn = new QPushButton(QStringLiteral("Apply"));
        applyBtn->setObjectName(QStringLiteral("wizardButton"));
        applyBtn->setCursor(Qt::PointingHandCursor);
        applyBtn->setEnabled(false);
        auto *recText = new QLabel;
        recText->setObjectName(QStringLiteral("hintLabel"));
        recText->setWordWrap(true);
        connect(detectBtn, &QPushButton::clicked, this, [this, applyBtn, recText] {
            const hw::Info info = hw::detect();
            const double r = hw::benchSingleThreadR();
            m_rec = hw::recommend(info, r);
            const QString mode = m_rec.windowMode == 2 ? QStringLiteral("Fullscreen")
                               : m_rec.windowMode == 1 ? QStringLiteral("Borderless")
                                                       : QStringLiteral("Windowed");
            recText->setText(QStringLiteral("%1 \u2014 %2x \u00b7 widescreen %3 \u00b7 pack %4 \u00b7 %5 fps \u00b7 %6")
                                 .arg(m_rec.tierName)
                                 .arg(m_rec.renderScale)
                                 .arg(m_rec.widescreen ? QStringLiteral("ON") : QStringLiteral("OFF"))
                                 .arg(m_rec.texPackFull ? QStringLiteral("Full") : QStringLiteral("Off/Lite"))
                                 .arg(m_rec.fps60 ? 60 : 30)
                                 .arg(mode));
            applyBtn->setEnabled(true);
        });
        connect(applyBtn, &QPushButton::clicked, this, [this, applyBtn, recText] {
            rec::apply(m_rec);
            refreshStatus();
            recText->setText(QStringLiteral("Applied \u2713"));
            applyBtn->setEnabled(false);
        });
        row->addWidget(detectBtn);
        row->addWidget(applyBtn);
        row->addWidget(recText, 1);
        root->addLayout(row);
        root->addWidget(hintRow(QStringLiteral(
            "Detect probes your CPU/RAM/GPU and benchmarks single-thread; Apply writes the "
            "recommended ceiling (render scale, window mode, texture pack, fps) to the settings.")));
    }

    root->addStretch(1);
    scroll->setWidget(content);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    connect(m_renderer, &QComboBox::currentIndexChanged, this, [this](int) {
        SettingsManager::instance().setRenderer(m_renderer->currentData().toInt());
        refreshStatus();
    });

    refreshStatus();
}

void VideoTab::refreshStatus()
{
    const auto &s = SettingsManager::instance();
    const auto screens = QGuiApplication::screens();

    // Renderer
    {
        const int r = s.renderer();
        const char *name = "OpenGL (New)";
        if (r == SettingsManager::kRendererSoftware) name = "Software rasterizer";
        else if (r == SettingsManager::kRendererParallelGS) name = "paraLLEl-GS (Vulkan)";
        else if (r == SettingsManager::kRendererD3D11) name = "Direct3D 11";
        const bool soft = (r == SettingsManager::kRendererSoftware);
        paintDot(m_dot[0], soft ? 1 : 0);
        m_val[0]->setText(QString::fromLatin1(name));
        m_note[0]->setText(soft ? QStringLiteral("CPU path: no upscale")
                                : (r == SettingsManager::kRendererParallelGS
                                       ? QStringLiteral("Vulkan compute (default)")
                                       : QStringLiteral("own GL present")));
    }

    // Monitor
    {
        const int asked = s.monitor();
        const int count = screens.size();
        const int idx = count > 0 ? std::clamp(asked, 0, count - 1) : 0;
        const bool clamped = count > 0 && asked != idx;
        paintDot(m_dot[1], count > 0 ? (clamped ? 1 : 0) : 2);
        m_val[1]->setText(monitorValue(idx));
        m_note[1]->setText(monitorNote(idx) + (clamped ? QStringLiteral("  (requested monitor missing: clamped)")
                                                       : QString()));
    }

    // Resolution + window mode
    {
        const int mode = s.windowMode();
        QString val, note;
        if (mode == 2)
        {
            const QScreen *sc = screens.isEmpty() ? nullptr
                                                  : screens[std::clamp(s.monitor(), 0, int(screens.size()) - 1)];
            const QRect g = sc ? sc->geometry() : QRect();
            val = g.isValid() ? QStringLiteral("%1 x %2").arg(g.width()).arg(g.height())
                              : QStringLiteral("monitor mode");
            note = QStringLiteral("fullscreen");
        }
        else
        {
            const QScreen *sc = QGuiApplication::primaryScreen();
            const QRect g = sc ? sc->geometry() : QRect();
            const int w = s.windowW() > 0 ? s.windowW() : g.width();
            const int h = s.windowH() > 0 ? s.windowH() : g.height();
            val = QStringLiteral("%1 x %2").arg(w).arg(h);
            note = mode == 1 ? QStringLiteral("borderless windowed") : QStringLiteral("windowed");
        }
        paintDot(m_dot[2], 0);
        m_val[2]->setText(val);
        m_note[2]->setText(note);
    }

    // Upscale
    {
        const int r = s.renderer();
        const int scale = std::clamp(s.renderScale(), 1, 4);
        paintDot(m_dot[3], r == SettingsManager::kRendererSoftware ? 2
                             : r == SettingsManager::kRendererParallelGS ? 0 : 1);
        m_val[3]->setText(QStringLiteral("x%1").arg(scale));
        m_note[3]->setText(r == SettingsManager::kRendererSoftware
                               ? QStringLiteral("not available (software renderer)")
                           : r == SettingsManager::kRendererParallelGS
                               ? QStringLiteral("active (samples per pixel)")
                               : QStringLiteral("applies on restart"));
    }
}

void VideoTab::openDisplayDialog()
{
    auto *view = new DisplayView(this);
    view->onApplied = [this] { refreshStatus(); };
    view->onBack = [] { viewhost::pop(); };
    viewhost::show(view);
}

void VideoTab::openVisualEffectsDialog()
{
    auto *view = new EffectsView(this);
    view->onApplied = [this] { refreshStatus(); };
    view->onBack = [] { viewhost::pop(); };
    viewhost::show(view);
}

void VideoTab::openTexPackDialog()
{
    auto *view = new TexPackView(this);
    view->onApplied = [this] { refreshStatus(); };
    view->onBack = [] { viewhost::pop(); };
    viewhost::show(view);
}
