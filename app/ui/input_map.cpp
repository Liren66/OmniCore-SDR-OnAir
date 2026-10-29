// View > Controls: the keys, mouse buttons and wheel of the spectrum and waterfall, rebindable. The tables and rules are in input_map.h.
#include "input_map.h"
#include "../app.h"
#include "../i18n/i18n.h"
#include "../platform/platform.h"
#include "scale.h"
#include "theme.h"
#include <algorithm>
#include <cfloat>
#include <cstring>
#include <string>

namespace inputmap {

namespace {
Settings gS;
bool gLoaded = false, gOpen = false;
std::string gMsg;    // why the last key was refused
int gCapture = -1;   // the action whose binding is being recorded (-1: none)

void loadNow() {
    plat::Prefs& p = plat::basePrefs();
    gS.reset();
    loadBindings(gS, p.getS("ctlKeys", ""));
    loadWheel(gS, Area::Spectrum, p.getS("ctlWhSpec", ""));
    loadWheel(gS, Area::Waterfall, p.getS("ctlWhWf", ""));
    const long st = p.getI("ctlStep", 0);
    gS.stepHz = st >= 0 && st <= 100000000 ? (int)st : 0;
    gS.wheelNotch = (int)std::max(1L, std::min(5L, p.getI("ctlNotch", 1)));
    gLoaded = true;
}

uint8_t modsNow() {
    const ImGuiIO& io = ImGui::GetIO();
    return (uint8_t)((io.KeyShift ? M_Shift : 0) | (io.KeyCtrl ? M_Ctrl : 0) | (io.KeyAlt ? M_Alt : 0) | (io.KeySuper ? M_Super : 0));
}

bool isModifierKey(int k) { return (k >= ImGuiKey_LeftCtrl && k <= ImGuiKey_RightSuper) || (k >= ImGuiKey_ReservedForModCtrl && k <= ImGuiKey_ReservedForModSuper); }

// The next key or mouse button pressed, as a binding (nothing: kind 0). Escape cancels (returns true with kind 0).
bool capture(Binding& out) {
    for (int b = 2; b < 5; b++)
        if (ImGui::IsMouseClicked(b, false)) { out = mouse(b, modsNow()); return true; }
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart; k++) {
        if (isModifierKey(k) || (k >= ImGuiKey_MouseLeft && k <= ImGuiKey_MouseWheelY)) continue;
        if (!ImGui::IsKeyPressed((ImGuiKey)k, false)) continue;
        if (k == ImGuiKey_Escape) { out = Binding(); return true; }
        out = key((ImGuiKey)k, modsNow());
        return true;
    }
    return false;
}

// grey text that wraps at the window's edge
void hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

std::string display(const Binding& b) {
    std::string s = bindingText(b);
#ifdef __APPLE__
    // ImGui reports the Command key as Ctrl and the Control key as Super on a Mac
    auto swap = [&](const char* from, const char* to) { size_t p = s.find(from); if (p != std::string::npos) s.replace(p, strlen(from), to); };
    if (b.mods & M_Ctrl) swap("Ctrl+", "Cmd+");
    if (b.mods & M_Super) swap("Super+", "Ctrl+");
#endif
    return s;
}
}  // namespace

Settings& settings() {
    if (!gLoaded) loadNow();
    return gS;
}

void save() {
    plat::Prefs& p = plat::basePrefs();
    const Settings& s = settings();
    p.setS("ctlKeys", saveBindings(s));
    const Settings d;
    p.setS("ctlWhSpec", saveWheel(s, Area::Spectrum) == saveWheel(d, Area::Spectrum) ? "" : saveWheel(s, Area::Spectrum));
    p.setS("ctlWhWf", saveWheel(s, Area::Waterfall) == saveWheel(d, Area::Waterfall) ? "" : saveWheel(s, Area::Waterfall));
    p.setI("ctlStep", s.stepHz);
    p.setI("ctlNotch", s.wheelNotch);
    p.flush();
}

void reload() { loadNow(); }

bool& windowOpen() { return gOpen; }

bool pressed(Action a, bool repeat) {
    const Binding& b = settings().bind[(int)a];
    if (!b.bound() || gCapture >= 0) return false;
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return false;
    if (modsNow() != b.mods) return false;
    if (b.kind == 1) return ImGui::IsKeyPressed((ImGuiKey)b.code, repeat);
    return ImGui::IsMouseClicked(b.code, false);
}

void window() {
    if (!gOpen) { gCapture = -1; return; }
    Settings& s = settings();
    firstWindowSize(700 * gUi, 640 * gUi);
    if (!ImGui::Begin(trw("Controls"), &gOpen)) { ImGui::End(); return; }
    bool changed = false;
    ImGui::TextWrapped("%s", tr("Choose what the mouse wheel, the keys and the extra mouse buttons do on the spectrum and the waterfall. Nothing here is needed: the defaults are what OnAir has always done."));
    if (anyConflict(s)) ImGui::TextColored(ImVec4(1.f, 0.55f, 0.2f, 1.f), "%s", tr("Two actions use the same key or button: both will fire. The rows are marked below."));
    if (!gMsg.empty()) ImGui::TextColored(ImVec4(1.f, 0.55f, 0.2f, 1.f), "%s", tr(gMsg.c_str()));
    ImGui::Spacing();

    if (ImGui::CollapsingHeader(trw("Mouse wheel"), ImGuiTreeNodeFlags_DefaultOpen)) {
        hint(tr("By default the wheel zooms the frequency and the dB range together (Ctrl: frequency only, Shift: dB only). Pick Zoom the frequency only on the plain wheel to leave the dB range alone."));
        if (ImGui::BeginTable("##whl", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn(tr("Keys held"), ImGuiTableColumnFlags_WidthFixed, 150 * gUi);
            ImGui::TableSetupColumn(tr("Over the spectrum"));
            ImGui::TableSetupColumn(tr("Over the waterfall"));
            ImGui::TableHeadersRow();
            for (int c = 0; c < kCombos; c++) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(tr(comboName(c)));
                for (int ar = 0; ar < (int)Area::Count; ar++) {
                    ImGui::TableSetColumnIndex(1 + ar);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::PushID(ar * 16 + c);
                    if (ImGui::BeginCombo("##w", tr(wheelName((Wheel)s.wheel[ar][c])))) {
                        for (int w = 0; w < (int)Wheel::Count; w++) {
                            if (ImGui::Selectable(tr(wheelName((Wheel)w)), s.wheel[ar][c] == w)) { s.wheel[ar][c] = (uint8_t)w; changed = true; }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("Tuning step"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160 * gUi);
        char cur[40];
        if (s.stepHz <= 0) snprintf(cur, sizeof cur, "%s", tr("The mode's own step"));
        else if (s.stepHz >= 1000000) snprintf(cur, sizeof cur, "%g MHz", s.stepHz / 1e6);
        else if (s.stepHz >= 1000) snprintf(cur, sizeof cur, "%g kHz", s.stepHz / 1e3);
        else snprintf(cur, sizeof cur, "%d Hz", s.stepHz);
        if (ImGui::BeginCombo("##step", cur)) {
            if (ImGui::Selectable(tr("The mode's own step"), s.stepHz <= 0)) { s.stepHz = 0; changed = true; }
            for (int v : stepList()) {
                char l[32];
                if (v >= 1000000) snprintf(l, sizeof l, "%g MHz", v / 1e6); else if (v >= 1000) snprintf(l, sizeof l, "%g kHz", v / 1e3); else snprintf(l, sizeof l, "%d Hz", v);
                if (ImGui::Selectable(l, s.stepHz == v)) { s.stepHz = v; changed = true; }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70 * gUi);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("Wheel notches per step"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70 * gUi);
        if (ImGui::SliderInt("##notch", &s.wheelNotch, 1, 5)) changed = true;
        hint(tr("The step is used by Tune by the step on the wheel and by the tune up / down and step keys below."));
    }

    if (ImGui::CollapsingHeader(trw("Keys and mouse buttons"), ImGuiTreeNodeFlags_DefaultOpen)) {
        hint(tr("Click Set, then press the key (with Ctrl, Shift or Alt if you like) or the middle / side mouse button. Esc cancels."));
        if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn(tr("Action"));
            ImGui::TableSetupColumn(tr("Binding"), ImGuiTableColumnFlags_WidthFixed, 190 * gUi);
            ImGui::TableSetupColumn("##b", ImGuiTableColumnFlags_WidthFixed, 150 * gUi);
            ImGui::TableHeadersRow();
            const char* lastGroup = "";
            for (int i = 0; i < kActions; i++) {
                const Def& d = defs()[i];
                if (strcmp(d.group, lastGroup) != 0) {
                    lastGroup = d.group;
                    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", tr(d.group));
                }
                ImGui::TableNextRow();
                ImGui::PushID(i);
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(tr(d.label));
                if (d.plotOnly) { ImGui::SameLine(); ImGui::TextDisabled("(%s)", tr("over the plots")); }
                const std::vector<int> cf = conflictsWith(s, i);
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                if (gCapture == i) ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.f, 1.f), "%s", tr("press a key ..."));
                else if (!s.bind[i].bound()) ImGui::TextDisabled("%s", tr("not set"));
                else if (!cf.empty()) ImGui::TextColored(ImVec4(1.f, 0.55f, 0.2f, 1.f), "%s", display(s.bind[i]).c_str());
                else ImGui::TextUnformatted(display(s.bind[i]).c_str());
                if (!cf.empty() && ImGui::IsItemHovered()) {
                    std::string t = tr("Also used by:");
                    for (int o : cf) t += std::string("\n  ") + tr(defs()[o].label);
                    ImGui::SetTooltip("%s", t.c_str());
                }
                ImGui::TableSetColumnIndex(2);
                if (gCapture == i) {
                    Binding nb;
                    if (capture(nb)) {
                        gCapture = -1;
                        if (nb.bound()) {
                            if (const char* why = reservedReason(nb)) gMsg = why;
                            else { s.bind[i] = nb; changed = true; gMsg.clear(); }
                        }
                    }
                    if (ImGui::SmallButton(tr("Cancel"))) gCapture = -1;
                } else {
                    if (ImGui::SmallButton(tr("Set"))) gCapture = i;
                    ImGui::SameLine();
                    if (ImGui::SmallButton(tr("Clear"))) { s.bind[i] = Binding(); changed = true; }
                    if (s.bind[i] != d.def) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton(tr("Default"))) { s.bind[i] = d.def; changed = true; }
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        hint(tr("Not available for rebinding: the left and right mouse buttons, Esc, Ctrl+K (command palette), Ctrl+Shift+L (layout)."));
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(s.isDefault());
    if (ImGui::Button(tr("Reset everything to the defaults"))) { s.reset(); gCapture = -1; changed = true; }
    ImGui::EndDisabled();
    if (changed) save();
    ImGui::End();
}

}  // namespace inputmap
