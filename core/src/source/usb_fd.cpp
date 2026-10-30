#include "dect2/source/usb_fd.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <mutex>

namespace dect2 {
namespace {
std::mutex gMu;
std::vector<UsbFdRadio>& reg() { static std::vector<UsbFdRadio> v; return v; }
}

bool usbFdAttach(const UsbFdRadio& r) {
    if (r.fd < 0 || r.name.empty()) return false;
    std::lock_guard<std::mutex> lk(gMu);
    auto& v = reg();
    for (auto& e : v) if (e.name == r.name) { e = r; return true; }
    v.push_back(r);
    return true;
}

void usbFdDetach(const std::string& name) {
    std::lock_guard<std::mutex> lk(gMu);
    auto& v = reg();
    v.erase(std::remove_if(v.begin(), v.end(), [&](const UsbFdRadio& e) { return e.name == name; }), v.end());
}

std::vector<UsbFdRadio> usbFdList(const std::string& kind) {
    std::lock_guard<std::mutex> lk(gMu);
    std::vector<UsbFdRadio> out;
    for (const auto& e : reg()) if (kind.empty() || e.kind == kind) out.push_back(e);
    return out;
}

std::string usbFdKindForId(int vid, int pid) {
    // RTL2832U: the Realtek ids (generic sticks, RTL-SDR Blog V3 / V4, NESDR) and the DVB-T sticks of librtlsdr's list of known devices
    static const int rtl[][2] = {
        {0x0bda, 0x2832}, {0x0bda, 0x2838},
        {0x0ccd, 0x00a9}, {0x0ccd, 0x00b3}, {0x0ccd, 0x00d3}, {0x0ccd, 0x00e0},   // Terratec
        {0x185b, 0x0620}, {0x185b, 0x0650},                                       // Compro
        {0x1f4d, 0xb803}, {0x1f4d, 0xc803},                                       // GTek
        {0x1b80, 0xd3a4},                                                         // Twintech
        {0x1d19, 0x1101}, {0x1d19, 0x1102}, {0x1d19, 0x1103},                     // Dexatek
        {0x0458, 0x707f},                                                         // Genius
        {0x15f4, 0x0131}, {0x15f4, 0x0133},                                       // HanfTek
    };
    for (const auto& p : rtl) if (vid == p[0] && pid == p[1]) return "rtlsdr";
    if (vid == 0x1d50 && pid == 0x6089) return "hackrf";                      // HackRF One and HackRF Pro (the board id tells them apart)
    if (vid == 0x1d50 && (pid == 0xcc15 || pid == 0x604b)) return "hackrf";   // rad1o, Jawbreaker
    if (vid == 0x1d50 && pid == 0x60a1) return "airspy";                      // Airspy R2 and Mini
    if (vid == 0x03eb && pid == 0x800c) return "airspyhf";                    // Airspy HF+ (Dual, Discovery)
    if (vid == 0x1a86 && pid == 0x5537) return "omnicore";
    return "";
}

bool usbFdPlatform() {
#if defined(__ANDROID__)
    return true;
#else
    const char* e = getenv("DECT2_USB_FD");
    return e && *e == '1';
#endif
}

bool usbFdPick(const std::string& kind, const std::string& serialWish, UsbFdRadio& out, std::string& err) {
    const auto v = usbFdList(kind);
    const std::string what = kind == "hackrf" ? "HackRF" : kind == "airspy" ? "Airspy" : kind == "airspyhf" ? "Airspy HF+" : kind == "rtlsdr" ? "RTL-SDR" : kind == "omnicore" ? "OmniCore SDR" : "radio";
    if (v.empty()) {
        err = "No " + what + " is connected, or Android has not given OnAir permission for it yet: plug it in (through an OTG adapter) and tap Allow";
        return false;
    }
    if (serialWish.empty()) { out = v[0]; return true; }
    for (const auto& r : v) {
        const std::string& sn = r.serial;
        if (sn.size() >= serialWish.size() && sn.compare(sn.size() - serialWish.size(), serialWish.size(), serialWish) == 0) { out = r; return true; }
    }
    err = "The " + what + " with serial number " + serialWish + " is not connected (" + std::to_string(v.size()) + " other one" + (v.size() == 1 ? "" : "s") + " are)";
    return false;
}

std::string usbFdErrorText(int code) {
    switch (code) {
    case 0: return "";
    case -3: return "Android has not given OnAir access to the radio: unplug it, plug it in again and tap Allow";
    case -4: return "The radio is gone: check the cable and the OTG adapter. If it drops out by itself, the phone may not give it enough "
                    "power: try a powered USB hub or a Y cable";
    case -5: return "The radio was not found (it may have been unplugged)";
    case -6: return "Another app is using the radio (an rtl_tcp driver app, another SDR app): close it, then try again";
    case -7: return "The radio does not answer: unplug it and plug it in again. A long or thin cable, or an unpowered hub, can cause this";
    case -8: return "The radio sent more data than asked for (overflow): unplug it and plug it in again";
    case -9: return "The radio refused the request (its firmware may be too old for this driver)";
    case -11: return "The phone ran out of memory for the USB transfers: close other apps and try again";
    case -12: return "This phone's USB stack does not support what the driver asked for";
    case -1: return "USB input/output error: check the cable and the OTG adapter, then plug the radio in again";
    case -2: return "The USB request was not valid (a driver problem: please report it with the log)";
    default: return "USB error " + std::to_string(code) + ": unplug the radio and plug it in again";
    }
}

uint64_t usbFdAirspySerial(const std::string& s) {
    // libairspy reads the serial from the board (two 32-bit words) and the USB string is "AIRSPY SN:" + 16 hex digits; take the last run of hex
    size_t end = s.size();
    while (end > 0 && !isxdigit((unsigned char)s[end - 1])) end--;
    size_t beg = end;
    while (beg > 0 && isxdigit((unsigned char)s[beg - 1]) && end - beg < 16) beg--;
    if (beg == end) return 0;
    return strtoull(s.substr(beg, end - beg).c_str(), nullptr, 16);
}

} // namespace dect2
