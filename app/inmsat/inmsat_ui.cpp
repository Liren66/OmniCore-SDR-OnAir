// Inmarsat (all carriers) screens: Carriers (a strip of the whole capture and the table of every carrier found, click one to focus it), Messages
// (ACARS and ADS-C from all P channels, Inmarsat-C EGC), Aircraft (map and table from the position reports) and Voice (the C-channel calls, mute / solo,
// recording, the user-installed codec). One Auto button restarts the search; the CPU budget and the Inmarsat-C switch are under Advanced.
// What the mode does and does not decode is said on the Voice tab and in docs/INMARSAT.md. See docs/INMARSAT-REVAMP.md.
#include "../adsb/adsb_map.h"
#include "../panels/picsave_ui.h"
#include "app.h"
#include "gfx/gfx.h"
#include "platform/platform.h"
#include "dect2/common/time_fmt.h"
#include "dect2/inmsat/inmsat_rx.h"
#include "dect2/inmsat/inmsat_tel.h"
#include "dect2/inmsat/inmsat_voice.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace {

struct State {
    bool loaded = false;
    int view = 0;                        // 0 carriers, 1 messages, 2 aircraft, 3 voice
    int focusId = 0;
    int pushedFocus = -1;
    double budget = 10;
    double pushedBudget = -1;
    bool inmcOn = true;
    int pushedInmc = -1;
    char satChoice[48] = "";             // manual satellite, "" = automatic
    std::string pushedSat = "?";
    double pushedLat = -999, pushedLon = -999;
    bool record = false;
    int pushedRecord = -1;
    char filter[24] = "";
    adsbmap::View map;
    bool mapFollow = true;
    uint32_t selAes = 0;
};
State S;

bool live(const App& a) { return a.rx.standard == 209; }   // the engine reports its standard code minus one

void load() {
    if (S.loaded) return;
    S.loaded = true;
    S.budget = (double)std::max(2L, std::min(24L, plat::prefs().getI("inmsatBudget", 10)));
    S.inmcOn = plat::prefs().getI("inmsatInmc", 1) != 0;
    S.record = plat::prefs().getI("inmsatRecord", 0) != 0;
    snprintf(S.satChoice, sizeof S.satChoice, "%s", plat::prefs().getS("inmsatSat", "").c_str());
}

const char* stateName(int s) { return s >= 3 ? "data" : s == 2 ? "frames in sync" : s == 1 ? "carrier locked" : "seen"; }

std::string idleText(double s) {
    if (s < 0) return "-";
    char b[24];
    if (s < 90) snprintf(b, sizeof b, "%.0f s", s); else snprintf(b, sizeof b, "%.0f min", s / 60);
    return b;
}

bool contains(const std::string& hay, const char* needle) {
    if (!*needle) return true;
    std::string h = hay, n = needle;
    for (auto& c : h) c = (char)std::tolower((unsigned char)c);
    for (auto& c : n) c = (char)std::tolower((unsigned char)c);
    return h.find(n) != std::string::npos;
}

void header(App& a) {
    ImGui::AlignTextToFramePadding();
    if (ImGui::Button("Auto##inmsat")) {
        S.focusId = 0;
        a.engine.inmsat().reset();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "Automatic is the normal mode: every carrier in the capture is found and decoded. This button forgets the carriers and searches again\n(after you moved the antenna or changed the gain). Messages stay.");
    flowNext(6 * gUi);
    if (ImGui::Button("Clear##inmsat")) a.engine.inmsat().clearResults();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "Empty the message, aircraft and log-on lists.");
    flowNext(10 * gUi);
    if (live(a)) {
        const dect2::InmsatTelemetry& t = a.rx.inmsat;
        ImGui::TextDisabled("%.2f Msps, %zu carriers, %zu windows, CPU %.1f of %.0f", t.rate / 1e6, t.carriers.size(), t.windows.size(), t.cpuCost, t.cpuBudget);
    }
    flowEnd();
}

// the whole capture as a strip: one bar per carrier, height by its level, green when decoding, amber locked, grey seen, red parked
void strip(App& a, const dect2::InmsatTelemetry& t) {
    const float h = 70 * gUi;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = std::max(100.f, ImGui::GetContentRegionAvail().x);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), IM_COL32(14, 16, 18, 255));
    const double half = 0.5 * t.rate;
    for (const auto& wi : t.windows) {
        const float x0 = p0.x + (float)((wi.centerHz - wi.bwHz / 2 + half) / t.rate) * w, x1 = p0.x + (float)((wi.centerHz + wi.bwHz / 2 + half) / t.rate) * w;
        dl->AddRectFilled(ImVec2(x0, p0.y), ImVec2(x1, p0.y + h), IM_COL32(60, 80, 110, 70));
    }
    for (const auto& c : t.carriers) {
        const float x = p0.x + (float)((c.offsetHz + half) / t.rate) * w;
        const float bh = std::min(h - 4, std::max(6.f, (c.snrDb / 25.f) * (h - 4)));
        const ImU32 col = c.parked ? IM_COL32(210, 80, 80, 255) : c.state >= 2 ? IM_COL32(90, 200, 120, 255) : c.state == 1 ? IM_COL32(230, 180, 70, 255) : IM_COL32(150, 154, 158, 255);
        dl->AddRectFilled(ImVec2(x - 1.5f * gUi, p0.y + h - bh), ImVec2(x + 1.5f * gUi, p0.y + h), col);
        if (c.id == S.focusId) dl->AddRect(ImVec2(x - 5 * gUi, p0.y + h - bh - 3), ImVec2(x + 5 * gUi, p0.y + h), IM_COL32(255, 255, 255, 220));
    }
    char lo[32], hi[32];
    const double c0 = t.centerHz != 0 ? t.centerHz / 1e6 : a.freqMhz;
    snprintf(lo, sizeof lo, "%.3f MHz", c0 - half / 1e6); snprintf(hi, sizeof hi, "%.3f MHz", c0 + half / 1e6);
    dl->AddText(ImVec2(p0.x + 4, p0.y + 2), IM_COL32(150, 154, 158, 255), lo);
    dl->AddText(ImVec2(p0.x + w - ImGui::CalcTextSize(hi).x - 4, p0.y + 2), IM_COL32(150, 154, 158, 255), hi);
    ImGui::Dummy(ImVec2(w, h));
}

// which satellite the capture is looking at, and why: the three kinds of evidence of the best candidates
void satellitePanel(const dect2::InmsatTelemetry& t) {
    const dect2::inmsat::SatResult& s = t.sat;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Satellite");
    ImGui::SameLine(0, 8 * gUi);
    if (s.valid) {
        ImGui::Text("%s", s.name.c_str());
        char lon[24]; snprintf(lon, sizeof lon, "%.1f%c", std::fabs(s.lonDeg), s.lonDeg < 0 ? 'W' : 'E');
        ImGui::SameLine(0, 8 * gUi); ImGui::TextDisabled("%s", lon);
        if (!s.beam.empty()) { ImGui::SameLine(0, 8 * gUi); ImGui::TextDisabled("%s", s.beam.c_str()); }
        ImGui::SameLine(0, 8 * gUi);
        if (s.manual) ImGui::TextDisabled("chosen by you");
        else ImGui::TextColored(s.confidence >= 0.7 ? ImVec4(0.45f, 0.85f, 0.55f, 1) : s.confidence >= 0.4 ? ImVec4(0.95f, 0.8f, 0.4f, 1) : ImVec4(0.9f, 0.5f, 0.4f, 1), "%.0f%% sure%s", s.confidence * 100, s.ambiguous ? ", close call" : "");
    } else {
        ImGui::TextDisabled("%s", s.summary.c_str());
    }
    {
        if (!s.ranked.empty() && ImGui::TreeNode("Evidence##inmsatev")) {
            const size_t n = std::min<size_t>(3, s.ranked.size());
            for (size_t i = 0; i < n; i++) {
                const auto& c = s.ranked[i];
                const auto& sat = dect2::inmsat::satTable()[(size_t)c.index];
                ImGui::Text("%s  %.0f%%", sat.name, c.confidence * 100);
                for (const auto& e : c.evidence) ImGui::TextDisabled("    %s: %s%s", e.kind.c_str(), e.available ? "" : "(no data) ", e.text.c_str());
            }
            ImGui::TextDisabled("%s", "Tables are unverified: needs a user test with a real capture.");
            ImGui::TreePop();
        }
    }
}

void carriersView(App& a) {
    const dect2::InmsatTelemetry& t = a.rx.inmsat;
    satellitePanel(t);
    strip(a, t);
    ImGui::Spacing();
    if (t.carriers.empty()) { ImGui::TextDisabled("%s", "no carriers yet: the search needs two seconds. Tune the radio to the middle of the satellite's L-band channels (about 1545.6 MHz) and use a sample rate of 6 Msps or more."); return; }
    if (!ImGui::BeginTable("##inmc", 11, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV,
                           ImVec2(0, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Frequency"); ImGui::TableSetupColumn("Satellite"); ImGui::TableSetupColumn("Type"); ImGui::TableSetupColumn("Rate"); ImGui::TableSetupColumn("Level"); ImGui::TableSetupColumn("Eb/N0");
    ImGui::TableSetupColumn("State"); ImGui::TableSetupColumn("Frames"); ImGui::TableSetupColumn("Last"); ImGui::TableSetupColumn("Width"); ImGui::TableSetupColumn("Notes", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    ImGui::PushFont(a.mono, 0);
    std::vector<dect2::inmsat::CarrierRow> rows = t.carriers;
    std::sort(rows.begin(), rows.end(), [](const dect2::inmsat::CarrierRow& x, const dect2::inmsat::CarrierRow& y) { return x.offsetHz < y.offsetHz; });
    for (const auto& c : rows) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        char f[48];
        if (c.freqHz > 0) snprintf(f, sizeof f, "%.4f MHz##ic%d", c.freqHz / 1e6, c.id); else snprintf(f, sizeof f, "%+.1f kHz##ic%d", c.offsetHz / 1e3, c.id);
        if (ImGui::Selectable(f, c.id == S.focusId, ImGuiSelectableFlags_SpanAllColumns)) { S.focusId = c.id == S.focusId ? 0 : c.id; }
        ImGui::TableNextColumn(); ImGui::TextUnformatted(c.satellite.empty() ? "-" : c.satellite.c_str());
        ImGui::TableNextColumn(); ImGui::TextUnformatted(dect2::inmsat::carrierTypeName(c.type));
        ImGui::TableNextColumn(); if (c.bitRate) ImGui::Text("%d", c.bitRate); else ImGui::TextDisabled("-");
        ImGui::TableNextColumn(); ImGui::Text("%.0f dB", c.snrDb);
        ImGui::TableNextColumn(); if (c.state >= 2) ImGui::Text("%.1f dB", c.ebn0Db); else ImGui::TextDisabled("-");
        ImGui::TableNextColumn(); ImGui::TextUnformatted(c.parked ? "parked: CPU" : stateName(c.state));
        ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)c.frames);
        ImGui::TableNextColumn(); ImGui::TextUnformatted(idleText(c.idleSec).c_str());
        ImGui::TableNextColumn(); ImGui::Text("%.1f kHz", c.widthHz / 1e3);
        ImGui::TableNextColumn();
        std::string note = c.region;
        if (!c.station.empty()) note += (note.empty() ? "" : ", ") + c.station;
        if (c.voiceCandidate) note += std::string(note.empty() ? "" : ", ") + "may be a voice (C) channel: not decoded";
        if (c.type == dect2::inmsat::CarrierType::Unknown && !c.voiceCandidate && !c.parked && c.state < 1) note += std::string(note.empty() ? "" : ", ") + dect2::inmsat::carrierClassName(c.cls);
        ImGui::TextDisabled("%s", note.c_str());
    }
    ImGui::PopFont();
    ImGui::EndTable();
}

void messagesView(App& a) {
    const dect2::InmsatTelemetry& t = a.rx.inmsat;
    ImGui::TextDisabled("Filter"); ImGui::SameLine(); ImGui::SetNextItemWidth(160 * gUi);
    ImGui::InputText("##inmflt", S.filter, sizeof S.filter);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "Shows messages whose registration, flight, label or text contains this.");
    ImGui::SameLine(); ImGui::TextDisabled("%zu ACARS / ADS-C, %zu Inmarsat-C", t.messages.size(), t.egc.size());
    if (!ImGui::BeginTable("##inmmsg", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV, ImVec2(0, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("UTC"); ImGui::TableSetupColumn("Kind"); ImGui::TableSetupColumn("Aircraft / area"); ImGui::TableSetupColumn("Label"); ImGui::TableSetupColumn("Rate");
    ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    ImGui::PushFont(a.mono, 0);
    for (size_t i = t.messages.size(); i-- > 0;) {
        const auto& m = t.messages[i];
        const std::string hay = m.registration + " " + m.flight + " " + m.label + " " + m.text + " " + m.decoded;
        if (!contains(hay, S.filter)) continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextDisabled("%s", dect2::timefmt::clock(m.wallTime).c_str());
        ImGui::TableNextColumn(); ImGui::TextUnformatted(m.decoded.empty() ? "ACARS" : "ADS-C");
        ImGui::TableNextColumn(); ImGui::Text("%s %s", m.registration.c_str(), m.flight.c_str());
        ImGui::TableNextColumn(); ImGui::Text("%s", m.label.c_str());
        ImGui::TableNextColumn(); ImGui::Text("%d", m.bitRate);
        ImGui::TableNextColumn(); ImGui::TextWrapped("%s", (m.decoded.empty() ? m.text : m.decoded).c_str());
    }
    for (const auto& e : t.egc) {
        if (!contains(e.serviceText + " " + e.text + " " + e.navarea + " " + e.subject, S.filter)) continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextDisabled("%s", e.rxTime.c_str());
        ImGui::TableNextColumn(); ImGui::TextUnformatted(e.kind == 1 ? "SafetyNET" : e.kind == 2 ? "FleetNET" : "Inmarsat-C");
        ImGui::TableNextColumn(); ImGui::Text("%s", e.navarea.c_str());
        ImGui::TableNextColumn(); ImGui::Text("%s", e.ident.c_str());
        ImGui::TableNextColumn(); ImGui::TextDisabled("%s", e.complete ? "" : "part");
        ImGui::TableNextColumn(); ImGui::TextWrapped("%s", e.text.c_str());
    }
    ImGui::PopFont();
    ImGui::EndTable();
}

void mapCanvas(App& a, ImVec2 size) {
    const dect2::InmsatTelemetry& t = a.rx.inmsat;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    if (S.mapFollow) {
        double la0 = 90, la1 = -90, lo0 = 180, lo1 = -180; int n = 0;
        for (const auto& c : t.aircraft) if (c.hasPos) { la0 = std::min(la0, c.lat); la1 = std::max(la1, c.lat); lo0 = std::min(lo0, c.lon); lo1 = std::max(lo1, c.lon); n++; }
        if (n) {
            S.map.lat = (la0 + la1) / 2; S.map.lon = (lo0 + lo1) / 2;
            const double span = std::max(0.5, std::max(lo1 - lo0, (la1 - la0) * 1.4));
            S.map.zoom = std::max(2, std::min(10, (int)std::floor(std::log2(0.7 * size.x * 360.0 / (256.0 * span)))));
        }
    }
    const double lat0 = S.map.lat, lon0 = S.map.lon;
    adsbmap::draw(S.map, size);
    if (S.map.lat != lat0 || S.map.lon != lon0) S.mapFollow = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p0, ImVec2(p0.x + size.x, p0.y + size.y), true);
    ImVec2 click; const bool clicked = adsbmap::clickedAt(click);
    const dect2::AeroAircraft* hit = nullptr; float best = 18 * gUi;
    int placed = 0;
    for (const auto& c : t.aircraft) {
        if (!c.hasPos) continue;
        placed++;
        const ImU32 col = adsbmap::acColour(adsbmap::AcSrc::Sat, t.timeSec - c.posTime, 0.5f);
        for (size_t i = 1; i < c.track.size(); i++)
            dl->AddLine(adsbmap::project(c.track[i - 1].lat, c.track[i - 1].lon), adsbmap::project(c.track[i].lat, c.track[i].lon), col, 1.5f);
    }
    for (const auto& c : t.aircraft) {
        if (!c.hasPos) continue;
        const ImVec2 q = adsbmap::project(c.lat, c.lon);
        if (q.x < p0.x - 60 || q.y < p0.y - 60 || q.x > p0.x + size.x + 60 || q.y > p0.y + size.y + 60) continue;
        const bool sel = S.selAes && c.aesId == S.selAes;
        dl->AddCircleFilled(q, 5 * gUi, adsbmap::acColour(adsbmap::AcSrc::Sat, t.timeSec - c.posTime));
        dl->AddCircle(q, 5 * gUi, sel ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 150), 16, sel ? 1.6f : 1.f);
        if (S.map.zoom >= 5 || sel || placed <= 12) {
            std::string s = !c.flight.empty() ? c.flight : !c.registration.empty() ? c.registration : "?";
            if (c.hasAlt) { char b[24]; snprintf(b, sizeof b, "  FL%03d", (c.altFt + 50) / 100); s += b; }
            const ImVec2 ts = ImGui::CalcTextSize(s.c_str());
            dl->AddRectFilled(ImVec2(q.x + 9 * gUi, q.y - ts.y / 2 - 2), ImVec2(q.x + 17 * gUi + ts.x, q.y + ts.y / 2 + 2), IM_COL32(10, 12, 14, 210), 2.f);
            dl->AddText(ImVec2(q.x + 13 * gUi, q.y - ts.y / 2), IM_COL32(235, 238, 240, 255), s.c_str());
        }
        const float d = std::hypot(q.x - click.x, q.y - click.y);
        if (clicked && d < best) { best = d; hit = &c; }
    }
    if (clicked) S.selAes = hit ? hit->aesId : 0;
    dl->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(p0.x + 8 * gUi, p0.y + 8 * gUi));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.07f, 0.08f, 0.85f));
    if (ImGui::Button("+##inmz", ImVec2(26 * gUi, 0))) { S.map.zoom = std::min(adsbmap::kMaxZoom, S.map.zoom + 1); S.mapFollow = false; }
    ImGui::SameLine(0, 2 * gUi);
    if (ImGui::Button("-##inmz", ImVec2(26 * gUi, 0))) { S.map.zoom = std::max(adsbmap::kMinZoom, S.map.zoom - 1); S.mapFollow = false; }
    ImGui::SameLine(0, 6 * gUi);
    if (ImGui::Button("fit##inmz")) S.mapFollow = true;
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 6 * gUi);
    adsbmap::tileControls("never the position itself");
    ImGui::SetCursorScreenPos(ImVec2(p0.x + 8 * gUi, p0.y + size.y - ImGui::GetTextLineHeight() - 6 * gUi));
    if (!placed) adsbmap::legend(size.x, "no position report yet (ADS-C and text reports come every few minutes)");
    else adsbmap::legend(size.x, "%d of %zu aircraft placed", placed, t.aircraft.size());
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + size.y));
    ImGui::Dummy(ImVec2(0, 0));
}

void aircraftView(App& a) {
    const dect2::InmsatTelemetry& t = a.rx.inmsat;
    const float h = ImGui::GetContentRegionAvail().y;
    mapCanvas(a, ImVec2(ImGui::GetContentRegionAvail().x, std::max(120.f * gUi, h * 0.55f)));
    if (!ImGui::BeginTable("##inmair", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV, ImVec2(0, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Aircraft"); ImGui::TableSetupColumn("Registration"); ImGui::TableSetupColumn("Position"); ImGui::TableSetupColumn("Altitude");
    ImGui::TableSetupColumn("Source"); ImGui::TableSetupColumn("Messages"); ImGui::TableSetupColumn("Last", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    ImGui::PushFont(a.mono, 0);
    for (const auto& c : t.aircraft) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        char id[40]; snprintf(id, sizeof id, "%s##inma%06X", c.flight.empty() ? (c.aesId ? "-" : "?") : c.flight.c_str(), c.aesId);
        if (ImGui::Selectable(id, S.selAes && c.aesId == S.selAes, ImGuiSelectableFlags_SpanAllColumns)) { S.selAes = c.aesId; if (c.hasPos) { S.map.lat = c.lat; S.map.lon = c.lon; S.mapFollow = false; } }
        ImGui::TableNextColumn(); ImGui::TextUnformatted(c.registration.c_str());
        ImGui::TableNextColumn(); if (c.hasPos) ImGui::Text("%.3f %.3f", c.lat, c.lon); else ImGui::TextDisabled("-");
        ImGui::TableNextColumn(); if (c.hasAlt) ImGui::Text("FL%03d", (c.altFt + 50) / 100); else ImGui::TextDisabled("-");
        ImGui::TableNextColumn(); ImGui::TextDisabled("%s", c.posKind.c_str());
        ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)c.messages);
        ImGui::TableNextColumn(); ImGui::TextDisabled("%s", dect2::timefmt::clock(c.lastWall).c_str());
    }
    ImGui::PopFont();
    ImGui::EndTable();
}

void voiceView(App& a) {
    const dect2::InmsatTelemetry& t = a.rx.inmsat;
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", "Aero C channels carry circuit-mode voice from the ground station to the aircraft. OnAir finds and lists these carriers, but it cannot demodulate them yet: "
                              "the C-channel modulation and framing are not in a public specification we may reproduce. The mixer, the per-call recorder and the codec hook below are built and tested "
                              "without a real C-channel signal, and need a user test with a real capture. Voice also needs the AMBE codec, which you install yourself (docs/CODECS.md). Unencrypted traffic only.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (!t.voiceMessage.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1), "%s", t.voiceMessage.c_str()); ImGui::PopTextWrapPos(); }
    else if (t.codecReady) ImGui::TextDisabled("%s", "Codec loaded.");
    if (ImGui::Checkbox("Record calls##inmrec", &S.record)) plat::prefs().setI("inmsatRecord", S.record ? 1 : 0);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "Writes each call to <Save pictures to>/Inmarsat as a WAV (with the codec) and always as the raw coded frames (.bits), so a capture is not wasted.");
    ImGui::Spacing();
    int cand = 0;
    for (const auto& c : t.carriers) cand += c.voiceCandidate;
    ImGui::TextDisabled("%d carrier%s may be voice channels (wide, no data decoder in sync).", cand, cand == 1 ? "" : "s");
    if (t.voice.empty()) { ImGui::TextDisabled("%s", "no calls"); return; }
    if (!ImGui::BeginTable("##inmv", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) return;
    ImGui::TableSetupColumn("Carrier"); ImGui::TableSetupColumn("State"); ImGui::TableSetupColumn("Frames"); ImGui::TableSetupColumn("Level"); ImGui::TableSetupColumn("Mute");
    ImGui::TableSetupColumn("Solo"); ImGui::TableSetupColumn("Recording", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (const auto& v : t.voice) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::Text("%.4f MHz", v.freqHz / 1e6);
        ImGui::TableNextColumn(); ImGui::TextUnformatted(v.active ? "call" : "idle");
        ImGui::TableNextColumn(); ImGui::Text("%llu / %llu", (unsigned long long)v.decoded, (unsigned long long)v.frames);
        ImGui::TableNextColumn(); ImGui::ProgressBar(v.level, ImVec2(60 * gUi, 8 * gUi), "");
        ImGui::TableNextColumn(); bool m = v.mute; char id[24]; snprintf(id, sizeof id, "##vm%d", v.id);
        if (ImGui::Checkbox(id, &m)) a.engine.inmsat().voice().setMute(v.id, m);
        ImGui::TableNextColumn(); bool s = v.solo; snprintf(id, sizeof id, "##vs%d", v.id);
        if (ImGui::Checkbox(id, &s)) a.engine.inmsat().voice().setSolo(v.id, s);
        ImGui::TableNextColumn(); ImGui::TextDisabled("%s", v.recording.empty() ? v.rawFile.c_str() : v.recording.c_str());
    }
    ImGui::EndTable();
}

void tick(App& a) {
    load();
    if (!a.engine.running()) { S.pushedFocus = -1; S.pushedBudget = -1; S.pushedInmc = -1; S.pushedRecord = -1; return; }
    dect2::InmsatReceiver& r = a.engine.inmsat();
    if (S.focusId != S.pushedFocus) { r.setFocus(S.focusId); S.pushedFocus = S.focusId; }
    if (S.budget != S.pushedBudget) { r.setBudget(S.budget); S.pushedBudget = S.budget; }
    if ((int)S.inmcOn != S.pushedInmc) { r.setInmarsatC(S.inmcOn); S.pushedInmc = (int)S.inmcOn; }
    if (S.pushedSat != S.satChoice) { r.setSatelliteOverride(S.satChoice); S.pushedSat = S.satChoice; }
    { double la = 0, lo = 0; const bool known = receiverLocationSet(); if (known) receiverLocation(la, lo);
      if (la != S.pushedLat || lo != S.pushedLon) { r.setLocation(known, la, lo); S.pushedLat = la; S.pushedLon = lo; } }
    if ((int)S.record != S.pushedRecord) { r.voice().setRecording(S.record ? picui::folder() + "/Inmarsat" : std::string()); S.pushedRecord = (int)S.record; }
}

void tuner(App&, bool&) {
    load();
    if (ImGui::CollapsingHeader("Advanced##inmsat")) {
        ImGui::TextDisabled("CPU budget"); ImGui::SameLine(); ImGui::SetNextItemWidth(120 * gUi);
        float b = (float)S.budget;
        if (ImGui::SliderFloat("##inmbud", &b, 2.f, 24.f, "%.0f")) { S.budget = std::round(b); plat::prefs().setI("inmsatBudget", (long)S.budget); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "How much decoding runs at once. One window of carriers (an Aero receiver) costs 1, an added Inmarsat-C receiver 0.6.\nCarriers that do not fit are listed as parked. The focused carrier is served first.");
        ImGui::TextDisabled("Satellite"); ImGui::SameLine(); ImGui::SetNextItemWidth(220 * gUi);
        if (ImGui::BeginCombo("##inmsatsel", S.satChoice[0] ? S.satChoice : "Automatic")) {
            if (ImGui::Selectable("Automatic", !S.satChoice[0])) { S.satChoice[0] = 0; plat::prefs().setS("inmsatSat", ""); }
            for (const auto& s : dect2::inmsat::satTable())
                if (ImGui::Selectable(s.name, !strcmp(S.satChoice, s.name))) { snprintf(S.satChoice, sizeof S.satChoice, "%s", s.name); plat::prefs().setS("inmsatSat", s.name); }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "Normally found automatically from the decoded system table, the carrier frequencies and where the satellite stands in your sky (set your location under View > Map). Choose one here to override it.");
        if (ImGui::Checkbox("Inmarsat-C##inmc", &S.inmcOn)) plat::prefs().setI("inmsatInmc", S.inmcOn ? 1 : 0);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", "Also decode the 2 kHz Inmarsat-C channels (EGC safety and news broadcasts) found in the capture.");
    }
}

void tab(App& a) {
    load();
    header(a);
    ImGui::Spacing();
    subNav("inmsat", S.view, {"Carriers", "Messages", "Aircraft", "Voice"});
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }
    if (a.rx.inmsat.seq == 0) { ImGui::TextDisabled("%s", "searching for carriers"); return; }
    switch (S.view) {
        case 0: carriersView(a); break;
        case 1: messagesView(a); break;
        case 2: aircraftView(a); break;
        default: voiceView(a); break;
    }
}

void list(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }
    const dect2::InmsatTelemetry& t = a.rx.inmsat;
    if (!ImGui::BeginTable("##inml", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit, ImVec2(0, ImGui::GetContentRegionAvail().y))) return;
    ImGui::TableSetupColumn("Aircraft"); ImGui::TableSetupColumn("Msgs"); ImGui::TableSetupColumn("Last", ImGuiTableColumnFlags_WidthStretch);
    for (const auto& c : t.aircraft) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::PushFont(a.mono, 0);
        char id[48]; snprintf(id, sizeof id, "%s##inmal%06X", !c.flight.empty() ? c.flight.c_str() : !c.registration.empty() ? c.registration.c_str() : "?", c.aesId);
        if (ImGui::Selectable(id, S.selAes && c.aesId == S.selAes)) S.selAes = c.aesId;
        ImGui::PopFont();
        ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)c.messages);
        ImGui::TableNextColumn(); ImGui::TextDisabled("%s", dect2::timefmt::clock(c.lastWall).c_str());
    }
    ImGui::EndTable();
    if (t.aircraft.empty()) ImGui::TextDisabled("%s", "no aircraft yet");
}

void receiver(App& a) {
    if (!live(a)) { ImGui::TextDisabled("%s", a.engine.running() ? "starting" : "stopped"); return; }
    const dect2::InmsatTelemetry& t = a.rx.inmsat;
    auto kv = [&](const char* k, const char* fmt, auto... v) { ImGui::TextDisabled("%s", k); kvColumn(130 * gUi); ImGui::PushFont(a.mono, 0); ImGui::PushTextWrapPos(0); ImGui::Text(fmt, v...); ImGui::PopTextWrapPos(); ImGui::PopFont(); };
    int dec = 0, voice = 0, parked = 0;
    for (const auto& c : t.carriers) { dec += c.state >= 2; voice += c.voiceCandidate; parked += c.parked; }
    kv("capture", "%.2f Msps around %.4f MHz", t.rate / 1e6, t.centerHz / 1e6);
    kv("carriers", "%zu found, %d decoding, %d parked, %d maybe voice", t.carriers.size(), dec, parked, voice);
    kv("windows", "%zu of cost %.1f (budget %.0f)", t.windows.size(), t.cpuCost, t.cpuBudget);
    kv("signal units", "%llu good, %llu failed the check", (unsigned long long)t.blocksOk, (unsigned long long)t.blocksBad);
    kv("messages", "%llu ACARS, %llu with a position, %zu Inmarsat-C", (unsigned long long)t.messagesTotal, (unsigned long long)t.positionsTotal, t.egc.size());
    kv("signal time", "%.1f s", t.timeSec);
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Decoded: Aero P channels (600, 1200, 10500 bit/s) and Inmarsat-C, all carriers of the capture at once. Not decoded: Aero C (voice) channels, R and T channels (aircraft to satellite, 1626 to 1660 MHz).");
    ImGui::PopTextWrapPos();
}

void status(App& a) {
    const bool run = a.engine.running(), on = live(a);
    const SignalStats& st = a.spec.stats;
    const AdcStatus adc = classifyAdc(st.rmsDbfs, st.peak, st.clipFraction);
    StatusPanel panel;
    const dect2::InmsatTelemetry& t = a.rx.inmsat;
    int dec = 0;
    for (const auto& c : t.carriers) dec += c.state >= 2;
    lamp("IQ", run ? (adc == AdcStatus::Overload ? 3 : (adc == AdcStatus::Good ? 1 : 2)) : 0, (int)Ic::Wave); flowNext(12 * gUi);
    lamp("Carriers", !on ? 0 : t.carriers.empty() ? 0 : 1); flowNext(12 * gUi);
    lamp("Decoding", !on ? 0 : dec > 0 ? 1 : 0); flowNext(12 * gUi);
    lamp("Messages", !on ? 0 : t.messagesTotal > 0 ? 1 : 0); flowNext(10 * gUi);
    ImGui::TextDisabled("|"); flowNext(10 * gUi);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", "State"); ImGui::SameLine(0, 5 * gUi);
    ImGui::PushFont(a.mono, 0); ImGui::TextUnformatted(!on ? (run ? "starting" : "stopped") : dect2::inmsatSummary(t).c_str()); ImGui::PopFont();
}

void summary(const App& a, std::string& l1, std::string& l2) {
    l1 = "Inmarsat";
    if (live(a)) l2 = dect2::inmsatSummary(a.rx.inmsat);
}

void meters(const App& a, std::vector<ModeMeter>& out) {
    const dect2::InmsatTelemetry& t = a.rx.inmsat;
    int dec = 0;
    for (const auto& c : t.carriers) dec += c.state >= 2;
    out.push_back({"CARRIERS", "%.0f", (double)t.carriers.size(), 0, 24, t.carriers.empty() ? 0 : 1});
    out.push_back({"DECODING", "%.0f", (double)dec, 0, 24, dec > 0 ? 1 : 0});
    out.push_back({"Eb/N0  dB", "%.0f", t.snrDb, 0, 30, t.snrDb >= 10 ? 1 : t.snrDb >= 6 ? 2 : 3});
    out.push_back({"AIRCRAFT", "%.0f", (double)t.aircraft.size(), 0, 60, t.aircraft.empty() ? 0 : 1});
}

} // namespace

extern const ModeUi kInmsatUi;
const ModeUi kInmsatUi = {
    .sideTitle = "AIRCRAFT",
    .tabName = "Inmarsat",
    .tabIcon = Ic::Radio,
    .tab = tab,
    .receiver = receiver,
    .list = list,
    .status = status,
    .summary = summary,
    .tuner = tuner,
    .tick = tick,
    .meters = meters,
};
