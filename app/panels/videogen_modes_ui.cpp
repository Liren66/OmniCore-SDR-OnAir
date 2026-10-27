// Tools > Digital video generator, tab "Digital Television Mode Selector" (videogen_modes_ui.h). Left: the link (bitrate, bandwidth,
// C/N). The inputs are kept in <data dir>/videogen_modes.txt.
#include "videogen_modes_ui.h"
#include "../app.h"
#include "../platform/platform.h"
#include "dect2/common/u8file.h"
#include "dect2/tv/dvb_modes.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace dect2::vgen;

namespace {

const char* const kLinkHelp = N_("What the link offers. The bitrate is what has to be carried: the generator's TS mux rate unless you untick it. The bandwidth is the room you have on the spectrum (a channel, a band-plan segment). The C/N is the ratio of the carrier to the noise you see on the spectrum analyser, in the same bandwidth as the signal.");

struct Sel {
    bool init = false;
    bool fromGenerator = true;   // the bitrate follows the generator's mux rate
    double mbps = 1.3, bwMHz = 8, cnDb = 20;
    std::string saved;
    double lastSave = 0;
};

Sel& S() {
    static Sel s;
    return s;
}

std::string cfgPath() { return plat::dataDir() + "/videogen_modes.txt"; }

std::string saveText(const Sel& s) {
    std::array<char, 160> b{};
    snprintf(b.data(), b.size(), "gen=%d\nmbps=%.6f\nbw=%.6f\ncn=%.3f\n", s.fromGenerator ? 1 : 0, s.mbps, s.bwMHz, s.cnDb);
    return b.data();
}

void loadText(const std::string& text, Sel& s) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "gen") s.fromGenerator = atoi(v.c_str()) != 0;
        else if (k == "mbps") s.mbps = std::clamp(atof(v.c_str()), 0.001, 1000.0);
        else if (k == "bw") s.bwMHz = std::clamp(atof(v.c_str()), 0.01, 1000.0);
        else if (k == "cn") s.cnDb = std::clamp(atof(v.c_str()), -20.0, 60.0);
    }
}

void loadOnce(Sel& s) {
    if (s.init) return;
    s.init = true;
    std::string t;
    if (FILE* f = dect2::u8fopen(cfgPath(), "rb")) {
        std::array<char, 1024> b{};
        size_t n;
        while ((n = fread(b.data(), 1, b.size(), f)) > 0) t.append(b.data(), n);
        fclose(f);
    }
    loadText(t, s);
    s.saved = saveText(s);
}

void saveSoon(Sel& s) {
    const double now = ImGui::GetTime();
    if (now - s.lastSave < 1.0) return;
    s.lastSave = now;
    const std::string t = saveText(s);
    if (t == s.saved) return;
    if (FILE* f = dect2::u8fopen(cfgPath(), "wb")) { fwrite(t.data(), 1, t.size(), f); fclose(f); }
    s.saved = t;
}

// A dimmed "(?)" after the last item, its text wrapped in a tooltip (as in the generator's other tabs)
void helpMark(const char* text) {
    ImGui::SameLine(0, 4 * gUi);
    ImGui::TextDisabled("(?)");
    if (!ImGui::IsItemHovered()) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380 * gUi);
    ImGui::TextUnformatted(tr(text));
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

bool numberRow(const char* label, const char* id, double& v, const char* fmt, double lo, double hi, float col, float w) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr(label));
    ImGui::SameLine(col);
    ImGui::SetNextItemWidth(w);
    if (!ImGui::InputDouble(id, &v, 0, 0, fmt)) return false;
    v = std::clamp(v, lo, hi);
    return true;
}

void linkInputs(Sel& s, int generatorKbps) {
    ImGui::SeparatorText(tr("Link"));
    helpMark(kLinkHelp);
    const float col = 110 * gUi, w = std::max(90 * gUi, ImGui::GetContentRegionAvail().x - col);
    if (s.fromGenerator) s.mbps = generatorKbps / 1000.0;
    ImGui::BeginDisabled(s.fromGenerator);
    numberRow(N_("Bitrate"), "##dmsrate", s.mbps, "%.3f Mbit/s", 0.001, 1000, col, w);
    ImGui::EndDisabled();
    ImGui::Checkbox(tr("The generator's TS mux rate"), &s.fromGenerator);
    numberRow(N_("Bandwidth"), "##dmsbw", s.bwMHz, "%.3f MHz", 0.01, 1000, col, w);
    numberRow(N_("C/N"), "##dmscn", s.cnDb, "%.1f dB", -20, 60, col, w);
}

} // namespace

void dvbModeSelectorTab(int generatorKbps) {
    Sel& s = S();
    loadOnce(s);
    const float left = std::min(330 * gUi, ImGui::GetContentRegionAvail().x * 0.35f);
    ImGui::BeginChild("##dmsinput", ImVec2(left, 0));
    linkInputs(s, generatorKbps);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##dmsmodes", ImVec2(0, 0));
    ImGui::EndChild();
    saveSoon(s);
}
