#pragma once

// Small ImGui widget set shared by the front-end pages. Same visual language as the in-game
// overlay (ps2_settings_overlay.cpp) on purpose: one HUD, two hosts.

#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>

namespace fe
{
    inline ImVec4 dbz(float r, float g, float b, float a = 1.0f) { return ImVec4(r, g, b, a); }
    inline ImVec4 accent(float a = 1.0f) { return dbz(1.00f, 0.62f, 0.10f, a); }
    inline ImVec4 gold(float a = 1.0f) { return dbz(1.00f, 0.80f, 0.30f, a); }
    inline ImVec4 okCol() { return dbz(0.25f, 0.73f, 0.31f); }
    inline ImVec4 warnCol() { return dbz(0.82f, 0.60f, 0.13f); }
    inline ImVec4 badCol() { return dbz(0.97f, 0.32f, 0.29f); }

    // Every row lines up on the same label column, and the column is derived from the font size
    // so it follows the DPI scale the window already applies.
    inline float unit() { return ImGui::GetFontSize(); }
    inline float labelW() { return unit() * 15.0f; }

    // Draws the label and leaves the cursor on the control side of the row.
    inline void rowLabel(const char *label)
    {
        ImGui::TextUnformatted(label);
        ImGui::SameLine(labelW());
    }

    inline void sectionHeader(const char *label)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, accent());
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - unit() * 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Separator, accent(0.35f));
        ImGui::Separator();
        ImGui::PopStyleColor();
    }

    // Collapsible section. The open/closed state is remembered per label across visits, so a
    // user who tidied the page finds it the same way tomorrow. `help` (optional) puts a "?" on
    // the right that opens the explanation in a popup, which is how the long paragraphs stop
    // eating vertical space on the pages that have the most rows.
    inline bool &sectionState(const char *label, bool defaultOpen)
    {
        static std::map<const char *, bool> s_open;
        auto it = s_open.find(label);
        if (it == s_open.end())
            it = s_open.emplace(label, defaultOpen).first;
        return it->second;
    }

    inline bool beginSection(const char *label, bool defaultOpen, const char *help = nullptr)
    {
        bool &open = sectionState(label, defaultOpen);
        const float f = unit();
        const float h = f * 1.55f;
        const float helpW = help ? f * 2.0f : 0.0f;
        const ImVec2 avail = ImGui::GetContentRegionAvail();

        ImGui::PushID(label);
        if (ImGui::InvisibleButton("##hdr", ImVec2(avail.x - helpW - f * 0.5f, h)))
            open = !open;
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();

        ImDrawList *dl = ImGui::GetWindowDrawList();
        if (hovered)
            dl->AddRectFilled(a, b, ImGui::GetColorU32(ImVec4(accent().x, accent().y, accent().z, 0.10f)));
        dl->AddLine(ImVec2(a.x, b.y - 0.5f), ImVec2(b.x, b.y - 0.5f),
                    ImGui::GetColorU32(accent(open ? 0.45f : 0.20f)), 1.0f);

        // Disclosure triangle, then the label.
        const float ty = (a.y + b.y) * 0.5f;
        const float cx = a.x + f * 0.55f;
        const float s = f * 0.28f;
        if (open)
            dl->AddTriangleFilled(ImVec2(cx - s, ty - s * 0.6f), ImVec2(cx + s, ty - s * 0.6f),
                                  ImVec2(cx, ty + s * 0.8f), ImGui::GetColorU32(accent()));
        else
            dl->AddTriangleFilled(ImVec2(cx - s * 0.6f, ty - s), ImVec2(cx - s * 0.6f, ty + s),
                                  ImVec2(cx + s * 0.8f, ty), ImGui::GetColorU32(accent()));
        dl->AddText(ImGui::GetFont(), f, ImVec2(cx + f * 0.85f, (a.y + b.y) * 0.5f - f * 0.5f),
                    ImGui::GetColorU32(open ? accent() : dbz(0.62f, 0.66f, 0.68f)), label);

        if (help)
        {
            ImGui::SameLine(a.x + avail.x - helpW);
            ImGui::PushStyleColor(ImGuiCol_Button, dbz(0.10f, 0.12f, 0.14f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent(0.30f));
            ImGui::PushStyleColor(ImGuiCol_Text, dbz(0.55f, 0.62f, 0.66f));
            if (ImGui::SmallButton("?"))
                ImGui::OpenPopup("##fe_help");
            ImGui::PopStyleColor(3);
            const bool helpVisible = ImGui::BeginPopup("##fe_help");
            if (helpVisible)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, gold());
                ImGui::TextUnformatted(label);
                ImGui::PopStyleColor();
                ImGui::Separator();
                ImGui::TextWrapped("%s", help);
                ImGui::Separator();
                if (ImGui::Button("Close", ImVec2(90.0f, 0.0f)))
                    ImGui::CloseCurrentPopup();
                // Inside the if: BeginPopup returning false opened no window, so ending one here
                // would be EndPopup() in the parent window, once per section per frame.
                ImGui::EndPopup();
            }
        }
        ImGui::PopID();
        return open;
    }

    // COLLAPSE / EXPAND ALL for the page a section belongs to.
    inline void allSectionsToggle(const char *const *labels, int count, bool wantOpen)
    {
        if (!ImGui::SmallButton(wantOpen ? "EXPAND ALL" : "COLLAPSE ALL"))
            return;
        for (int i = 0; i < count; ++i)
            sectionState(labels[i], wantOpen);
    }

    inline void hint(const char *text)
    {
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, dbz(0.29f, 0.39f, 0.44f));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }

    inline void hintPending(const char *text)
    {
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, warnCol());
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }

    // Checkbox with an ON/OFF badge, the same read as the overlay's toggleSwitch.
    inline bool toggleSwitch(const char *label, bool *v)
    {
        rowLabel(label);
        ImGui::PushID(label);
        const bool changed = ImGui::Checkbox("##v", v);
        ImGui::SameLine();
        const bool on = *v;
        ImGui::PushStyleColor(ImGuiCol_Button, on ? accent() : dbz(0.25f, 0.25f, 0.32f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? gold() : dbz(0.30f, 0.30f, 0.38f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, on ? gold() : dbz(0.30f, 0.30f, 0.38f));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? dbz(0.10f, 0.07f, 0.03f) : dbz(0.85f, 0.85f, 0.85f));
        ImGui::SmallButton(on ? "ON" : "OFF");
        ImGui::PopStyleColor(4);
        ImGui::PopID();
        return changed;
    }

    inline bool comboRow(const char *label, int *value, const char *const *items, int count,
                         float width = 0.0f)
    {
        rowLabel(label);
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(width > 0.0f ? width : unit() * 16.0f);
        const bool changed = ImGui::Combo("##c", value, items, count);
        ImGui::PopID();
        return changed;
    }

    inline bool sliderRow(const char *label, float *value, float lo, float hi, const char *fmt,
                          float width = 0.0f)
    {
        rowLabel(label);
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(width > 0.0f ? width : unit() * 17.0f);
        const bool changed = ImGui::SliderFloat("##s", value, lo, hi, fmt);
        ImGui::PopID();
        return changed;
    }

    inline bool intSliderRow(const char *label, int *value, int lo, int hi, const char *fmt,
                             float width = 0.0f)
    {
        rowLabel(label);
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(width > 0.0f ? width : unit() * 17.0f);
        const bool changed = ImGui::SliderInt("##i", value, lo, hi, fmt);
        ImGui::PopID();
        return changed;
    }

    inline bool comboRowStr(const char *label, int *value, const char *const *items, int count,
                            const char *current, float width = 0.0f)
    {
        rowLabel(label);
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(width > 0.0f ? width : unit() * 16.0f);
        bool changed = false;
        const bool comboOpen = ImGui::BeginCombo("##cs", current);
        if (comboOpen)
        {
            for (int i = 0; i < count; ++i)
            {
                const bool sel = (*value == i);
                if (ImGui::Selectable(items[i], sel))
                {
                    *value = i;
                    changed = true;
                }
                if (sel)
                    ImGui::SetItemDefaultFocus();
            }
            // Inside the if, and that is the whole contract: a Begin* that returns false opened
            // no window, so there is nothing to end. EndCombo() also decrements
            // g.BeginComboDepth unconditionally, and BeginComboPopup names its popup
            // "##Combo_%02d" from that depth (imgui_widgets.cpp), so ending an unopened combo
            // every frame walks the depth negative and the popup can never hold a stable window:
            // the dropdown stops opening, and the popup it did open is left on the window stack
            // unended, which desyncs every Begin/End after it and kills hover for the whole app.
            ImGui::EndCombo();
        }
        ImGui::PopID();
        return changed;
    }

    inline void statusRow(const char *label, ImVec4 color, const char *value)
    {
        rowLabel(label);
        ImGui::TextColored(color, "%s", value);
    }

    inline void kv(const char *label, const char *value)
    {
        rowLabel(label);
        ImGui::TextDisabled("%s", value);
    }

    inline bool primaryButton(const char *label, const ImVec2 &size, bool enabled = true)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, dbz(0.18f, 0.55f, 0.30f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, dbz(0.25f, 0.73f, 0.40f));
        ImGui::PushStyleColor(ImGuiCol_Text, dbz(0.02f, 0.06f, 0.03f));
        ImGui::BeginDisabled(!enabled);
        const bool pressed = ImGui::Button(label, size);
        ImGui::EndDisabled();
        ImGui::PopStyleColor(3);
        return pressed;
    }

    inline void formatBytes(unsigned long long bytes, char *out, size_t cap)
    {
        if (bytes >= 1024ull * 1024ull * 1024ull)
            std::snprintf(out, cap, "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
        else if (bytes >= 1024ull * 1024ull)
            std::snprintf(out, cap, "%.1f MB", bytes / (1024.0 * 1024.0));
        else if (bytes >= 1024ull)
            std::snprintf(out, cap, "%.1f KB", bytes / 1024.0);
        else
            std::snprintf(out, cap, "%llu B", bytes);
    }

    // Screen/tab transitions. The shell runs a fade to black, performs the swap while the screen
    // is fully dark, then fades back in; PLAY instead stays dark and lets the game take over.
    class Fader
    {
    public:
        // outSec/inSec are the one-way legs; peak is how dark it gets (1 = full black).
        // False when a fade is already running, so callers can keep their pending state in sync.
        bool start(float peak, float outSec, float inSec)
        {
            if (m_phase != Phase::Idle)
                return false;
            m_peak = peak;
            m_outSec = outSec > 0.01f ? outSec : 0.01f;
            m_inSec = inSec > 0.01f ? inSec : 0.01f;
            m_phase = Phase::Out;
            return true;
        }
        // Stops the fade-in and holds the current darkness (used when the game takes over).
        void hold() { m_phase = Phase::Held; }

        // dt in seconds. Calls fire() once, at the moment the screen is fully dark.
        void tick(float dt, const std::function<void()> &fire)
        {
            switch (m_phase)
            {
            case Phase::Out:
                m_alpha += dt / m_outSec;
                if (m_alpha >= m_peak)
                {
                    m_alpha = m_peak;
                    m_phase = Phase::In;
                    if (fire)
                        fire();
                }
                break;
            case Phase::In:
                m_alpha -= dt / m_inSec;
                if (m_alpha <= 0.0f)
                {
                    m_alpha = 0.0f;
                    m_phase = Phase::Idle;
                }
                break;
            case Phase::Idle:
            case Phase::Held:
                break;
            }
        }

        bool busy() const { return m_phase == Phase::Out || m_phase == Phase::In; }
        float alpha() const { return m_alpha; }

        // Drawn on the foreground list, i.e. on top of every window and every widget. The
        // background list is painted *before* the windows, so a fade drawn there is invisible.
        // Leaving the foreground list untouched for a frame is enough for ImGui to drop it.
        void draw() const
        {
            if (m_alpha <= 0.001f)
                return;
            const ImGuiViewport *vp = ImGui::GetMainViewport();
            const ImVec2 tl(vp->Pos.x, vp->Pos.y);
            const ImVec2 br(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
            ImDrawList *dl = ImGui::GetForegroundDrawList();
            dl->AddRectFilled(tl, br, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, m_alpha)));
        }

    private:
        enum class Phase
        {
            Idle,
            Out,
            In,
            Held
        };
        Phase m_phase = Phase::Idle;
        float m_alpha = 0.0f;
        float m_peak = 0.0f;
        float m_outSec = 0.14f;
        float m_inSec = 0.18f;
    };
}
