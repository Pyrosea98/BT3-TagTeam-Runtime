// [fe-probe] Headless check on the front-end's ImGui widget lifecycle.
//
// comboRowStr and beginSection both call a Begin* and then an End*. The rule is that a Begin*
// which returns false opened no window, so there is nothing to end -- and breaking it is not a
// cosmetic error:
//
//   * EndCombo() decrements g.BeginComboDepth unconditionally, while BeginCombo() only increments
//     it when it really opens the popup, and BeginComboPopup names its popup "##Combo_%02d" from
//     that depth. Ending an unopened combo once per frame walks the depth negative until the
//     popup cannot hold a stable window, so the dropdown stops opening.
//   * On the frame the popup does open, the stray EndCombo() is the wrong window and ImGui bails
//     out of EndPopup() before touching the stack, so the popup window is left begun. Every
//     Begin/End after it is off by one and hover dies for the whole app: nothing is clickable.
//
// Both symptoms are timing- and page-dependent, so driving the real widgets through frames and
// asserting the invariants is the only way to catch a regression. Null backend: no window, no GL.
#include <cstdio>
#include <cstdlib>

#include "imgui.h"
#include "imgui_internal.h"   // BeginComboDepth, BeginPopupStack, CurrentWindowStack, error count
#include "imgui_impl_null.h"

#include "frontend/fe_ui.h"

namespace
{
int g_fail = 0;
int g_errors = 0;

void check(bool ok, const char *what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FALLA", what);
    if (!ok)
        ++g_fail;
}

int errors()
{
    return ImGui::GetCurrentContext()->ErrorCountCurrentFrame;
}

int comboDepth()
{
    return ImGui::GetCurrentContext()->BeginComboDepth;
}

int windowStack()
{
    return ImGui::GetCurrentContext()->CurrentWindowStack.Size;
}

void beginFrame()
{
    ImGui_ImplNullPlatform_NewFrame();
    ImGui_ImplNullRender_NewFrame();
    ImGui::NewFrame();
}

void endFrame()
{
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    g_errors += errors();
}
}   // namespace

int main()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    // Same posture as the launcher (see fe_window.cpp): tooltip and assert on, so a misuse is
    // reported rather than swallowed.
    io.ConfigErrorRecoveryEnableTooltip = true;
    io.ConfigErrorRecoveryEnableAssert = true;
    io.ConfigErrorRecoveryEnableDebugLog = false;   // no TTY here; we count errors ourselves

    ImGui_ImplNull_Init();
    ImGui_ImplNullRender_Init();

    static const char *const kRenders[] = {"Software", "OpenGL", "paraLLEl-GS"};
    static const char *const kLevels[] = {"ERROR", "WARN", "INFO", "DEBUG"};

    int renderer = 0;
    int level = 2;
    bool sectionOpen = true;
    int stackBaseline = -1;

    std::printf("[1] 300 frames with 3 collapsible sections and 3 combos each\n");
    for (int frame = 0; frame < 300; ++frame)
    {
        beginFrame();

        // Shaped like fe_app.cpp's host window, since that is the window a stray End* lands in and
        // reports itself against. The nesting is copied verbatim, including the two details that
        // only exist in the real app: the page child is sized (0,0) and the scroll child inside it
        // has a NEGATIVE height (it reserves room for the footer bar). Both make BeginChild resolve
        // its size from GetContentRegionAvail, so a child can come out zero-sized and be reported
        // as not visible while still being begun.
        ImGui::Begin("##fe_host");
        ImGui::BeginChild("##fe_side", ImVec2(190.0f, 400.0f), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted("side");
        ImGui::EndChild();
        if (ImGui::BeginChild("##fe_page", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
        {
            if (ImGui::BeginChild("##fe_scroll", ImVec2(0.0f, -30.0f), ImGuiChildFlags_None))
            {
                for (int s = 0; s < 3; ++s)
                {
                    char label[32];
                    std::snprintf(label, sizeof(label), "SECCION_%d", s);
                    const bool open = fe::beginSection(label, true, "help text");
                    if (s == 0)
                        sectionOpen = open;
                    if (open)
                    {
                        fe::comboRow("Motor", &renderer, kRenders, 3);
                        fe::comboRowStr("GPU", &renderer, kRenders, 3, kRenders[renderer]);
                        fe::comboRow("Level", &level, kLevels, 4);
                    }
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();
        ImGui::End();
        endFrame();

        if (stackBaseline < 0)
            stackBaseline = windowStack();

        if (comboDepth() != 0)
        {
            check(false, "BeginComboDepth returns to 0 after every frame");
            std::printf("       frame=%d depth=%d\n", frame, comboDepth());
            break;
        }
        if (windowStack() != stackBaseline)
        {
            check(false, "the window stack returns to its base after every frame");
            std::printf("       frame=%d stack=%d base=%d\n", frame, windowStack(), stackBaseline);
            break;
        }
    }
    check(comboDepth() == 0, "BeginComboDepth ends at 0 (it does not filter towards negative)");
    check(windowStack() == stackBaseline, "the window stack did not grow (nothing left unclosed)");
    check(ImGui::GetCurrentContext()->BeginPopupStack.Size == 0, "the popup stack ends up empty");
    check(sectionOpen, "the section stays as the user left it");

    std::printf("[2] no ImGui error during the whole run\n");
    check(g_errors == 0, "ErrorCountCurrentFrame stayed at 0 the whole time");
    if (g_errors)
        std::printf("       errores=%d\n", g_errors);

    ImGui_ImplNullRender_Shutdown();
    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();

    std::printf("%s (%d fallos)\n", g_fail ? "PROBE FAILED" : "PROBE OK", g_fail);
    return g_fail ? 1 : 0;
}
