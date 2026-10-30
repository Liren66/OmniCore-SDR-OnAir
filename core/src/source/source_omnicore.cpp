// Native driver for the OmniCoreSDR (AD9361 + Zynq + CH32H417 USB 3 bridge) through OmniCore Host (OCH): the radio's own library
// (omnicore.dll, libomnicore.so) is loaded at run time when it is installed. See native_common.h.
//
// One receive input (RX1 or RX2) and one transmit output (TX1 or TX2), at the same time if wanted (full duplex). OCH opens a radio once per
// program and both directions share its handle, its sample clock and its configuration (OmniRadio below). OCH cannot change which channels
// are in use while a stream exists, so starting or stopping one direction (or moving to the other input / output) stops both streams,
// configures the channels and starts the ones in use again: the receiver sees a short gap. Frequency, gain, sample rate, the analog filter
// and the gain mode change while streaming (OCH pauses and resumes its streams itself where it has to; since 0.4.6 a change of frequency or
// gain alone does not stop anything).
// Radio settings: the RX input, the AD9361's gain mode, the RX / TX analog filter bandwidth, the AD9361's RX DC and IQ (quadrature)
// tracking, and its RX RSSI (OCH 0.4.7), read while receiving and shown by the interface (omnicoreStatus, dect2/source/omnicore.h). The OCH structures and calls used are declared here (OCH ABI 1, include/omnicore/och.h of OmniCore Host 0.4.6), so building
// OnAir needs nothing from OmniCore Host.
#include "native_common.h"
#include "dect2/source/omnicore.h"
#include "dect2/source/usb_fd.h"
#include <functional>
#include <map>

namespace dect2 {
namespace native {

// ------------------------------------------------------------------ OCH ABI 1 (the parts OnAir uses)

namespace och {
enum { OK = 0, TIMEOUT = -1, INVALID = -2, NOT_FOUND = -3, BUSY = -4, IO = -5, UNSUPPORTED = -6, STOPPED = -7 };
enum { RX = 0, TX = 1 };
enum { FC32 = 2 };
enum { META_HOST_OVERFLOW = 4u, META_DEVICE_OVERFLOW = 8u };
enum { EVENT_HOST_OVERFLOW = 3, EVENT_TRANSPORT = 4, EVENT_TX_STARVED = 5 };
constexpr uint32_t kAbi = 1;
struct device;
struct stream;
struct device_info {
    uint32_t struct_size, abi_version;
    char serial[128], name[128], driver[32], firmware[128];
    uint32_t usb_speed_mbps;
    uint32_t rx_channels, tx_channels, hardware_timestamps;
    uint32_t max_sample_rate;
};
struct config {
    uint32_t struct_size, sample_rate, rx_mask, tx_mask;
    uint64_t rx_frequency, tx_frequency;
    uint32_t rx_bandwidth, tx_bandwidth;
    double rx_gain[2], tx_attenuation[2];
    uint32_t rx_agc;   // 0 manual, 1 fast attack, 2 slow attack, 3 hybrid
    uint32_t usb_iq_bits;
    uint32_t reserved[6];
};
struct stream_args {
    uint32_t struct_size, direction, format;
    uint32_t transfer_bytes, usb_queue_depth, host_buffer_bytes;
    uint32_t reserved[10];
};
struct metadata {
    uint32_t struct_size, flags;
    uint64_t epoch, sample_index, host_dropped_samples;
    int64_t device_dropped_samples;
    uint64_t host_monotonic_ns;
};
struct event {
    uint32_t struct_size, type;
    uint64_t sequence, host_monotonic_ns, epoch;
    char message[512];
};
struct rx_correction {
    uint32_t struct_size, dc_enabled, iq_enabled, reserved[5];
};
struct rx_rssi {   // OCH 0.4.7
    uint32_t struct_size, enabled, usb_rx_mask, valid[2];
    double rssi_db[2];
    uint32_t reserved[4];
};
static_assert(sizeof(config) == 104, "och_config is 104 bytes in OCH ABI 1");
static_assert(sizeof(rx_rssi) == 56, "och_rx_rssi is 56 bytes in OCH 0.4.7");
} // namespace och

struct OchApi {
    const char* (DECT2_CALL* version)() = nullptr;
    const char* (DECT2_CALL* last_error)() = nullptr;
    void (DECT2_CALL* default_config)(och::config*) = nullptr;
    void (DECT2_CALL* default_stream_args)(och::stream_args*, int) = nullptr;
    int (DECT2_CALL* list)(och::device_info*, size_t, size_t*) = nullptr;
    int (DECT2_CALL* open)(const char*, och::device**) = nullptr;
    int (DECT2_CALL* open_android_fd)(int, const char*, och::device**) = nullptr;
    int (DECT2_CALL* close)(och::device*) = nullptr;
    int (DECT2_CALL* get_info)(och::device*, och::device_info*) = nullptr;
    int (DECT2_CALL* configure)(och::device*, const och::config*) = nullptr;
    int (DECT2_CALL* get_config)(och::device*, och::config*) = nullptr;
    int (DECT2_CALL* poll_event)(och::device*, och::event*, uint32_t) = nullptr;
    int (DECT2_CALL* create_stream)(och::device*, const och::stream_args*, och::stream**) = nullptr;
    int (DECT2_CALL* destroy_stream)(och::stream*) = nullptr;
    int (DECT2_CALL* start)(och::stream*) = nullptr;
    int (DECT2_CALL* stop)(och::stream*, uint32_t) = nullptr;
    int (DECT2_CALL* flush)(och::stream*, uint32_t) = nullptr;
    int (DECT2_CALL* recv)(och::stream*, void*, size_t, size_t*, och::metadata*, uint32_t) = nullptr;
    int (DECT2_CALL* send)(och::stream*, const void*, size_t, size_t*, uint32_t) = nullptr;
    // optional (OCH 0.3.1 and later): the AD9361's RX DC and quadrature tracking
    int (DECT2_CALL* get_rx_correction)(och::device*, och::rx_correction*) = nullptr;
    int (DECT2_CALL* set_rx_dc_correction)(och::device*, uint32_t) = nullptr;
    int (DECT2_CALL* set_rx_iq_correction)(och::device*, uint32_t) = nullptr;
    // optional (OCH 0.4.7 and later): the AD9361's RX symbol RSSI
    int (DECT2_CALL* get_rx_rssi)(och::device*, och::rx_rssi*) = nullptr;
    int (DECT2_CALL* set_rx_rssi_enabled)(och::device*, uint32_t) = nullptr;
    bool ok = false;
    DynLib lib;
    OchApi() {
        std::vector<std::string> names = libNames("omnicore", {}, {"omnicore.dll"});
#if defined(_WIN32)
        // the OmniCore Host installer's folder (it also puts itself on the PATH, which the bare name above covers)
        if (const char* pf = getenv("ProgramFiles")) names.insert(names.end() - 1, std::string(pf) + "\\OmniCore Host\\bin\\omnicore.dll");
#elif !defined(__APPLE__) && !defined(__ANDROID__)
        // tools/build-linux.sh + cmake --install --prefix /opt/omnicore/<version>
        for (auto& p : versionedLibs("omnicore", {"/opt/omnicore/lib/"})) names.push_back(p);
        {
            glob_t g{};
            if (glob("/opt/omnicore/*/lib/libomnicore.so*", 0, nullptr, &g) == 0)
                for (size_t k = g.gl_pathc; k-- > 0;) names.push_back(g.gl_pathv[k]);   // newest version folder first
            globfree(&g);
        }
#endif
        if (nativeDisabled() || !lib.open(names)) return;
        ok = lib.get(version, "och_version") && lib.get(last_error, "och_last_error") && lib.get(default_config, "och_default_config") &&
             lib.get(default_stream_args, "och_default_stream_args") && lib.get(list, "och_list") && lib.get(open, "och_open") &&
             lib.get(close, "och_close") && lib.get(get_info, "och_get_info") && lib.get(configure, "och_configure") &&
             lib.get(get_config, "och_get_config") && lib.get(poll_event, "och_poll_event") && lib.get(create_stream, "och_create_stream") &&
             lib.get(destroy_stream, "och_destroy_stream") && lib.get(start, "och_start") && lib.get(stop, "och_stop") &&
             lib.get(flush, "och_flush") && lib.get(recv, "och_recv") && lib.get(send, "och_send");
        lib.opt(get_rx_correction, "och_get_rx_correction");
        lib.opt(set_rx_dc_correction, "och_set_rx_dc_correction");
        lib.opt(set_rx_iq_correction, "och_set_rx_iq_correction");
        lib.opt(get_rx_rssi, "och_get_rx_rssi");
        lib.opt(set_rx_rssi_enabled, "och_set_rx_rssi_enabled");
        lib.opt(open_android_fd, "och_open_android_fd");
        if (ok) {
            fprintf(stderr, "OmniCore Host: library %s\n", version());
            fflush(stderr);
        }
    }
    // the reason OCH gives for the last failure on this thread (copied at once: the next failure replaces it)
    std::string why(int code) const {
        const char* e = last_error ? last_error() : nullptr;
        std::string s = e && *e ? std::string(e) : std::string();
        return s.empty() ? "OCH error " + std::to_string(code) : s + " (" + std::to_string(code) + ")";
    }
};
OchApi& ochApi() { static OchApi a; return a; }

namespace {

constexpr double kMinFreqHz = 70e6, kMaxFreqHz = 6e9;   // what OCH accepts (the AD9361 in its AD9364 range)
// Without the AD9361's FIR the converter clock chain needs 12 x the rate >= 25 MHz: 2.083 Msps and up; OnAir resamples narrower channels
constexpr double kMinRateHz = 2.084e6, kMaxRateHz = 61.44e6;
constexpr double kMinBwHz = 0.2e6, kMaxBwHz = 56e6;
constexpr double kMaxAttenDb = 89.75;
constexpr double kUsb2RateHz = 14e6;   // OmniCore Host docs/usb2.md: one channel of 12-bit samples on USB 2.0 High-Speed
constexpr size_t kChunk = 1 << 15;     // samples per och_recv

std::string mhz(double hz) {
    char b[32];
    snprintf(b, sizeof b, "%.6g", hz / 1e6);
    return b;
}

// The highest manual RX gain of the AD9361's gain tables at f (as for the PlutoSDR); OCH refuses more, and a refused configuration stops
// both streams, so the gain is held within it
double maxRxGain(double f) { return f <= 1.3e9 ? 73 : f <= 4e9 ? 71 : 62; }

// An analog filter bandwidth: the radio setting (MHz, 0 = automatic) first, then what OnAir asks for, else 95 % of the sample rate
uint32_t filterHz(double settingMhz, double asked, double rate) {
    const double bw = settingMhz > 0 ? settingMhz * 1e6 : asked > 0 ? asked : rate * 0.95;
    return (uint32_t)std::llround(std::clamp(bw, kMinBwHz, kMaxBwHz));
}

uint32_t agcMode(const std::string& m) { return m == "fast" ? 1 : m == "slow" ? 2 : m == "hybrid" ? 3 : 0; }

RadioSetting bwSetting(const std::string& key, const std::string& label, const std::string& help) {
    RadioSetting r;
    r.type = RadioSetting::Number;
    r.key = key; r.label = label; r.help = help;
    r.minV = 0; r.maxV = 56; r.step = 0.1; r.unit = "MHz"; r.def = "0";
    return r;
}

std::vector<RadioSetting> omniSettings() {
    return {ppmSetting(0.01),
            choiceSetting("rxport", "RX input", "The receive input the antenna is on. Changing it restarts both streams for a moment.",
                          {"rx1", "rx2"}, {"RX1", "RX2"}, "rx1"),
            choiceSetting("gainmode", "Gain mode",
                          "Manual: the gain slider (and OnAir's AGC) set the AD9361's gain (the default).\n"
                          "Slow / fast attack, hybrid: the AD9361's own AGC sets it; the gain slider then has no effect.",
                          {"manual", "slow", "fast", "hybrid"}, {"Manual", "Radio AGC, slow attack", "Radio AGC, fast attack", "Radio AGC, hybrid"}, "manual"),
            bwSetting("rxbw", "RX analog bandwidth",
                      "The AD9361's receive analog filter, 0.2 to 56 MHz. 0 = automatic: the channel's bandwidth, else 95 % of the sample rate."),
            bwSetting("txbw", "TX analog bandwidth",
                      "The AD9361's transmit analog filter, 0.2 to 56 MHz. 0 = automatic: what the transmission asks for, else 95 % of the sample rate."),
            boolSetting("dccorr", "RX DC correction",
                        "The AD9361's RF and baseband DC offset tracking (both RX inputs). On by default: removes the spike at the centre.\n"
                        "Off: the correction found so far is kept but no longer updated. Needs OmniCore Host 0.3.1 and firmware with rx_correction.", true),
            boolSetting("iqcorr", "RX IQ correction",
                        "The AD9361's quadrature (IQ imbalance) tracking (both RX inputs). On by default: suppresses the mirror image of strong signals.\n"
                        "Off: the correction found so far is kept but no longer updated. Needs OmniCore Host 0.3.1 and firmware with rx_correction.", true),
            boolSetting("rssi", "RX RSSI",
                        "Reads the AD9361's RX signal strength (both receivers) twice a second while receiving and shows it under the radio settings.\n"
                        "Relative dB, higher is stronger: not a calibrated input power (the gain and the front end change it).\n"
                        "A query only: the stream is not interrupted. Needs OmniCore Host 0.4.7.", true)};
}

// ------------------------------------------------------------------ what the interface shows (omnicoreStatus)

std::mutex gStatusMu;
std::map<std::string, OmniCoreStatus> gStatus;   // per serial, while a receiver of this program runs on the radio
void noteStatus(const std::string& serial, const std::function<void(OmniCoreStatus&)>& f) { std::lock_guard<std::mutex> lk(gStatusMu); f(gStatus[serial]); }
void dropStatus(const std::string& serial) { std::lock_guard<std::mutex> lk(gStatusMu); gStatus.erase(serial); }

// ------------------------------------------------------------------ the radio both directions share

// One per serial number while a receiver or a transmitter uses it. Control and lifecycle calls go through ctl (OCH wants them from one
// thread at a time); och_recv and och_send run outside it, under rxIo / txIo, which rebuild() takes after stopping the streams so that no
// receive or send is inside a stream it destroys.
struct OmniRadio {
    och::device* dev = nullptr;
    std::string serial;
    och::device_info info{};
    std::mutex ctl;
    std::mutex rxIo, txIo;
    och::config cfg{};               // what is configured (the applied sample rate after each change)
    och::stream* rx = nullptr;       // (rxIo and ctl to change)
    och::stream* tx = nullptr;       // (txIo and ctl to change)
    std::atomic<bool> rxOn{false}, txOn{false};   // the directions in use (changed under ctl)
    uint32_t rxMask = 1, txMask = 1; // the channel each uses: 1 = RX1 / TX1, 2 = RX2 / TX2
    std::atomic<bool> rebuilding{false};
    std::atomic<uint64_t> streamGen{0};   // counts the stream rebuilds: a receive or send that failed in an old stream tries the new one
    // A control command that timed out leaves OCH's handle unable to send any other (every later one fails with IO until the radio is
    // opened again; seen on the radio about once in ten opens with firmware at 3.125 MBd). Set by link(): the receiver then opens the
    // radio again (NativeSource's open retry, or its reconnect when the stream thread ends).
    std::atomic<bool> linkLost{false};
    std::string linkError;           // OCH's words for it (ctl)
    int dc = -1, iq = -1;            // the RX tracking as last set (-1: not yet); ctl
    bool correctionNoted = false;
    int rssi = -1;                   // the RSSI reporting as last set (-1: not yet); ctl
    bool rssiWanted = true;          // the receiver's "rssi" setting (ctl)
    std::chrono::steady_clock::time_point rssiNext{};
    std::chrono::milliseconds rssiEvery{500};
    std::mutex evMu;
    std::chrono::steady_clock::time_point evNext{};

    ~OmniRadio() {
        auto& a = ochApi();
        if (rx) a.destroy_stream(rx);
        if (tx) a.destroy_stream(tx);
        if (dev) a.close(dev);
    }
    double rate() const { return cfg.sample_rate; }
    // (ctl) the result of a control call; a timeout or an I/O failure marks the control link lost
    int link(int r) {
        if ((r == och::TIMEOUT || r == och::IO) && !linkLost) {
            linkError = ochApi().why(r);
            linkLost = true;
            fprintf(stderr, "OmniCoreSDR: the control link stopped answering (%s): the radio is opened again\n", linkError.c_str());
            fflush(stderr);
        }
        return r;
    }

    // (ctl) Stops and destroys both streams, configures cfg with the channels of the directions in use and starts their streams again
    bool rebuild(std::string& err) {
        auto& a = ochApi();
        rebuilding = true;
        if (rx) a.stop(rx, 500);
        if (tx) a.stop(tx, 500);
        {
            std::lock_guard<std::mutex> r(rxIo), t(txIo);
            if (rx) { a.destroy_stream(rx); rx = nullptr; }
            if (tx) { a.destroy_stream(tx); tx = nullptr; }
        }
        streamGen++;
        bool ok = true;
        if (rxOn || txOn) {
            cfg.rx_mask = rxOn ? rxMask : 0;
            cfg.tx_mask = txOn ? txMask : 0;
            if (!txOn) cfg.tx_attenuation[0] = cfg.tx_attenuation[1] = kMaxAttenDb;
            ok = configure(err) && open(och::RX, rxOn, err) && open(och::TX, txOn, err);
            if (!ok) {
                std::lock_guard<std::mutex> r(rxIo), t(txIo);
                if (rx) { a.destroy_stream(rx); rx = nullptr; }
                if (tx) { a.destroy_stream(tx); tx = nullptr; }
            }
        }
        rebuilding = false;
        return ok;
    }
    // (ctl) cfg to the radio, with or without streams (the channels must not change while streams exist: rebuild() for that)
    bool configure(std::string& err) {
        auto& a = ochApi();
        if (const int r = link(a.configure(dev, &cfg)); r != och::OK) { err = "OmniCoreSDR: the configuration was refused: " + a.why(r); return false; }
        och::config got{};
        got.struct_size = sizeof got;
        if (a.get_config(dev, &got) == och::OK && got.sample_rate > 0) cfg.sample_rate = got.sample_rate;   // OCH may take the nearest rate its clocks can make
        return true;
    }
    // (ctl) the RX tracking: only a change is sent (a change pauses the streams for a moment); a firmware without it is noted once
    void corrections(bool dcOn, bool iqOn, std::string& note) {
        auto& a = ochApi();
        if (dc == (int)dcOn && iq == (int)iqOn) return;
        if (!a.set_rx_dc_correction || !a.set_rx_iq_correction) {
            if (!correctionNoted && (!dcOn || !iqOn)) { correctionNoted = true; addNote(note, "this OmniCore Host library has no RX DC / IQ correction calls (0.3.1 or later has them)"); }
            dc = dcOn; iq = iqOn;
            return;
        }
        for (int k = 0; k < 2; k++) {
            const bool want = k == 0 ? dcOn : iqOn;
            int& cur = k == 0 ? dc : iq;
            if (cur == (int)want) continue;
            const int r = link(k == 0 ? a.set_rx_dc_correction(dev, want) : a.set_rx_iq_correction(dev, want));
            if (r == och::OK) { cur = want; continue; }
            if (r == och::UNSUPPORTED) {
                if (!correctionNoted) { correctionNoted = true; addNote(note, "this OmniCoreSDR firmware has no RX DC / IQ correction control (rx_correction)"); }
                dc = dcOn; iq = iqOn;
                return;
            }
            addNote(note, std::string("OmniCoreSDR: the RX ") + (k == 0 ? "DC" : "IQ") + " correction was not set: " + a.why(r));
        }
    }
    // Takes the OCH events off the radio's queue (it is bounded), the trouble ones to the log; at most twice a second, from the stream threads
    void drainEvents() {
        std::unique_lock<std::mutex> lk(evMu, std::try_to_lock);
        if (!lk.owns_lock()) return;
        const auto now = std::chrono::steady_clock::now();
        if (now < evNext) return;
        evNext = now + std::chrono::milliseconds(500);
        och::event e{};
        for (int k = 0; k < 64; k++) {
            e.struct_size = sizeof e;
            if (ochApi().poll_event(dev, &e, 0) != och::OK) break;
            if (e.type == och::EVENT_TRANSPORT || e.type == och::EVENT_HOST_OVERFLOW || e.type == och::EVENT_TX_STARVED) {
                fprintf(stderr, "OmniCoreSDR: %s\n", trimmed(e.message, sizeof e.message).c_str());
                fflush(stderr);
            }
        }
    }
    // (ctl) the RX RSSI reporting, switched only when it changes. The SDK starts with it off; an SDK without the switch (OCH: UNSUPPORTED)
    // always reports, and OCH reads it from its status lines there
    void setRssi(bool on) {
        auto& a = ochApi();
        rssiWanted = on;
        if (!a.set_rx_rssi_enabled || rssi == (int)on) return;
        const int r = link(a.set_rx_rssi_enabled(dev, on));
        if (r != och::OK && r != och::UNSUPPORTED) { fprintf(stderr, "OmniCoreSDR: the RX RSSI was not switched %s: %s\n", on ? "on" : "off", a.why(r).c_str()); fflush(stderr); return; }
        rssi = on;
        rssiNext = {};
    }
    // Reads the RX RSSI into the interface's status, at most every rssiEvery; from the receive thread. It never waits for ctl (a retune
    // or a rebuild has it): the reading is skipped then. A slow reading (an old SDK, through its status lines at 115200 baud) is read less
    // often, so the radio's control link stays free for tuning.
    void pollRssi() {
        auto& a = ochApi();
        const auto now = std::chrono::steady_clock::now();
        if (now < rssiNext) return;
        std::unique_lock<std::mutex> lk(ctl, std::try_to_lock);
        if (!lk.owns_lock()) return;
        const int input = rxMask == 2 ? 1 : 0;
        if (!rssiWanted || !a.get_rx_rssi) {
            rssiNext = now + std::chrono::milliseconds(500);
            const bool lib = a.get_rx_rssi != nullptr, want = rssiWanted;
            noteStatus(serial, [&](OmniCoreStatus& st) {
                st.receiving = true; st.rssiOn = want; st.rssiKnown = false; st.input = input;
                st.note = !want ? "" : lib ? "" : "this OmniCore Host library has no RX RSSI (0.4.7 or later has it)";
            });
            return;
        }
        och::rx_rssi m{};
        m.struct_size = sizeof m;
        const int r = a.get_rx_rssi(dev, &m);
        const std::string why = r == och::OK ? "" : a.why(r);
        link(r);
        const auto took = std::chrono::steady_clock::now() - now;
        rssiEvery = std::clamp(std::chrono::duration_cast<std::chrono::milliseconds>(took * 10), std::chrono::milliseconds(500), std::chrono::milliseconds(5000));
        rssiNext = now + rssiEvery;
        lk.unlock();
        noteStatus(serial, [&](OmniCoreStatus& st) {
            st.receiving = true; st.rssiOn = true; st.input = input;
            st.rssiKnown = r == och::OK && m.enabled;
            for (int k = 0; k < 2; k++) { st.valid[k] = st.rssiKnown && m.valid[k]; st.rssiDb[k] = st.valid[k] ? m.rssi_db[k] : 0; }
            st.note = r == och::UNSUPPORTED ? "this OmniCoreSDR firmware does not report the RX RSSI"
                    : r != och::OK ? "the RX RSSI could not be read: " + why
                    : !m.enabled ? "the radio has its RSSI reporting off"
                    : "";
        });
    }

private:
    // OCH's default buffers hold seconds of transmit samples (64 MiB on the host and 32 USB transfers: 6 s at 4 Msps), and write() is
    // paced only once they are full: what OnAir sends would go out seconds late, and go on after a stop. A transmit stream gets transfers
    // of about 5 ms, 16 of them in flight and 32 on the host: about 0.25 s from write() to the antenna. Measured on the radio (USB 3,
    // 4 to 61.44 Msps): fewer in flight (8) or a host ring of about 10 transfers runs the USB queue dry hundreds of times in 6 s; the host
    // ring is counted in transfers because OCH divides it into whole transfers.
    static void txArgs(och::stream_args& args, double rate) {
        constexpr uint32_t kUnit = 49152, kMaxTransfer = 1048576;   // OCH: transfer_bytes a multiple of 49152, at most 1 MiB
        const double wireBytes = rate * 3;                         // 12-bit samples, one channel
        const uint32_t units = (uint32_t)std::clamp(std::lround(wireBytes * 0.005 / kUnit), 1L, (long)(kMaxTransfer / kUnit));
        args.transfer_bytes = units * kUnit;
        args.usb_queue_depth = 16;
        args.host_buffer_bytes = 32 * args.transfer_bytes;
    }
    bool open(int dir, bool on, std::string& err) {
        if (!on) return true;
        auto& a = ochApi();
        och::stream_args args{};
        a.default_stream_args(&args, dir);
        args.format = och::FC32;
        if (dir == och::TX) txArgs(args, cfg.sample_rate);
        och::stream* s = nullptr;
        if (const int r = a.create_stream(dev, &args, &s); r != och::OK || !s) {
            err = std::string("OmniCoreSDR: the ") + (dir == och::RX ? "receive" : "transmit") + " stream could not be created: " + a.why(r);
            return false;
        }
        if (dir == och::TX) {
            // och_start refuses a transmit stream with less than two USB transfers queued (2 x transfer_bytes of wire data): silence
            // first. One sample per transfer byte is more than that for every wire format (12-bit: 3 bytes a sample per channel).
            std::vector<cf32> zeros(args.transfer_bytes);
            size_t sent = 0;
            for (int k = 0; sent < zeros.size() && k < 100; k++) {
                size_t done = 0;
                const int r = a.send(s, zeros.data() + sent, zeros.size() - sent, &done, 200);
                sent += done;
                if (r != och::OK && r != och::TIMEOUT) break;
            }
        }
        if (const int r = a.start(s); r != och::OK) {
            err = std::string("OmniCoreSDR: the ") + (dir == och::RX ? "receive" : "transmit") + " stream did not start: " + a.why(r);
            a.destroy_stream(s);
            return false;
        }
        std::lock_guard<std::mutex> lk(dir == och::RX ? rxIo : txIo);
        (dir == och::RX ? rx : tx) = s;
        return true;
    }
};

std::mutex gMu;
std::map<std::string, std::weak_ptr<OmniRadio>> gRadios;

// The radio with this serial, opened if no receiver or transmitter of this program has it open yet
std::shared_ptr<OmniRadio> openRadio(const std::string& serial, std::string& err, bool& final) {
    auto& a = ochApi();
    final = false;
    if (!a.ok) { err = "OmniCoreSDR: the OmniCore Host library (omnicore) is missing: install OmniCore Host 0.4.6 or later"; final = true; return {}; }
    std::lock_guard<std::mutex> lk(gMu);
    if (auto r = gRadios[serial].lock()) return r;
    auto r = std::make_shared<OmniRadio>();
    r->serial = serial;
    int rc;
    if (usbFdPlatform()) {
        UsbFdRadio fd;
        if (!usbFdPick("omnicore", serial, fd, err)) { final = true; return {}; }
        if (!a.open_android_fd) { err = "OmniCore Host lacks Android USB descriptor support; install the OmniCore Android build"; final = true; return {}; }
        rc = a.open_android_fd(fd.fd, fd.serial.c_str(), &r->dev);
    } else rc = a.open(serial.empty() ? nullptr : serial.c_str(), &r->dev);
    if (rc != och::OK || !r->dev) {
        r->dev = nullptr;
        err = "OmniCoreSDR " + serial + ": cannot open the radio: " + a.why(rc) +
              (rc == och::BUSY ? " (another program, such as och.exe, has it open)" : "") +
              (kOnWindows && rc == och::NOT_FOUND ? " (check the libusbK driver with och-driver check)" : "");
        return {};
    }
    r->info.struct_size = sizeof r->info;
    if (a.get_info(r->dev, &r->info) == och::OK)
        fprintf(stderr, "OmniCoreSDR: opened %s, firmware %s, USB %u Mbit/s\n", trimmed(r->info.serial, sizeof r->info.serial).c_str(),
                trimmed(r->info.firmware, sizeof r->info.firmware).c_str(), r->info.usb_speed_mbps);
    else r->info = {};
    fflush(stderr);
    if (r->info.abi_version && r->info.abi_version != och::kAbi) {
        err = "OmniCoreSDR: this OmniCore Host library speaks ABI " + std::to_string(r->info.abi_version) + ", OnAir knows ABI 1";
        final = true;
        return {};
    }
    a.default_config(&r->cfg);
    r->cfg.rx_mask = r->cfg.tx_mask = 0;
    r->cfg.rx_agc = 0;
    r->cfg.tx_attenuation[0] = r->cfg.tx_attenuation[1] = kMaxAttenDb;
    for (auto it = gRadios.begin(); it != gRadios.end();) it = it->second.expired() ? gRadios.erase(it) : std::next(it);
    gRadios[serial] = r;
    return r;
}

// a note when the rate is more than a USB 2.0 link carries (the radio is still asked for it)
std::string usb2Note(const OmniRadio& r, double rate, int streams) {
    if (r.info.usb_speed_mbps != 480 || rate <= kUsb2RateHz / streams + 1) return "";
    return "this OmniCoreSDR is on USB 2.0, which carries about " + mhz(kUsb2RateHz / streams) + " Msps here without loss; samples may be lost at " +
           mhz(rate) + " Msps (a USB 3 port carries the full rate)";
}

} // namespace

// ------------------------------------------------------------------ receive

class OmniSource : public NativeSource {
public:
    explicit OmniSource(std::string serial) : serial_(std::move(serial)) {}
    ~OmniSource() override { stop(); }

    std::string statusNote() const override {
        const uint64_t h = hostOverflows_.load(), d = devOverflows_.load();
        if (!h && !d) return {};
        char b[160];
        snprintf(b, sizeof b, "OmniCoreSDR: %llu host / %llu radio overflows (samples lost)", (unsigned long long)h, (unsigned long long)d);
        return b;
    }

protected:
    bool openDevice(const TuneSettings& s, std::string& err) override {
        bool final = false;
        auto r = openRadio(serial_, err, final);
        if (!r) { if (final) openFailure_ = OpenFailure::Final; return false; }
        {
            std::lock_guard<std::mutex> lk(r->ctl);
            if (r->rxOn) { err = "OmniCoreSDR: the receive stream is already open"; openFailure_ = OpenFailure::Final; return false; }
        }
        radio_ = r;
        hostOverflows_ = devOverflows_ = 0;
        return configure(s, err, false);
    }
    // the shared sample clock: a receiver may use a running transmitter's rate when it is at least the one it needs, but not change it
    bool canRetune(const TuneSettings& s, std::string& err) override {
        if (!radio_) return true;
        std::lock_guard<std::mutex> lk(radio_->ctl);
        return rateFits(s, err);
    }
    // OCH changes the rate of running streams itself (it pauses and resumes them): no restart of the stream thread for a new rate
    bool restartForRate() const override { return false; }
    bool configure(const TuneSettings& s, std::string& err, bool live) override {
        if (!radio_) return false;
        OmniRadio& r = *radio_;
        std::lock_guard<std::mutex> lk(r.ctl);
        if (!rateFits(s, err)) { openFailure_ = OpenFailure::Final; return false; }
        // OCH refuses a frequency outside 70 MHz to 6 GHz, and a refused configuration stops both streams: such a frequency is refused here
        const double f = s.centerHz;
        const double tuneHz = ppmCorrectedHz(f, radioPpm(s));
        if (tuneHz < kMinFreqHz || tuneHz > kMaxFreqHz) {
            err = "OmniCoreSDR: the AD9361 tunes " + mhz(kMinFreqHz) + " to " + mhz(kMaxFreqHz) + " MHz; " + mhz(f) + " MHz cannot be set";
            if (!live) openFailure_ = OpenFailure::Final;
            return false;
        }
        const double want = r.txOn ? r.rate() : std::clamp(s.sampleRate > 0 ? s.sampleRate : 10e6, kMinRateHz, kMaxRateHz);
        och::config c = r.cfg;
        c.sample_rate = (uint32_t)std::llround(want);
        c.rx_frequency = (uint64_t)std::llround(tuneHz);
        c.rx_bandwidth = filterHz(radioNumber(s, "rxbw", 0), s.basebandFilterHz, want);
        c.rx_agc = agcMode(radioOption(s, "gainmode", "manual"));
        const double g = std::clamp(std::round(s.gainDb), 0.0, maxRxGain(tuneHz));
        c.rx_gain[0] = c.rx_gain[1] = g;
        const uint32_t mask = radioOption(s, "rxport", "rx1") == "rx2" ? 2 : 1;
        // the RX tracking and the RSSI switch before the streams are (re)started: OCH changes the tracking by pausing the streams, and a
        // control command just after a stream started timed out on the radio (firmware at 3.125 MBd)
        r.corrections(radioFlag(s, "dccorr", true), radioFlag(s, "iqcorr", true), err);
        r.setRssi(radioFlag(s, "rssi", true));
        const och::config before = r.cfg;
        r.cfg = c;
        bool ok = !r.linkLost;
        if (!ok) {}
        else if (!r.rxOn || mask != r.rxMask) {   // a new stream, or the other input: both streams are set up again
            r.rxOn = ownsRx_ = true;
            r.rxMask = mask;
            ok = r.rebuild(err);
            if (!ok) { r.rxOn = ownsRx_ = false; r.cfg = before; std::string e2; if (r.txOn) r.rebuild(e2); }
        } else {
            ok = r.configure(err);
            if (!ok) r.cfg = before;
        }
        if (!ok && !r.linkLost) return false;
        if (r.linkLost) {   // the handle can send no more commands: open the radio again (start(): its retry; a retune: the stream thread)
            err = "OmniCoreSDR: the radio's control link stopped answering (" + r.linkError + ")";
            if (!live) openFailure_ = OpenFailure::Retry;
            return false;
        }
        rate_ = r.rate();
        requested_ = s.sampleRate;
        checkRate(std::max(s.sampleRate, kMinRateHz), err);
        addNote(err, usb2Note(r, rate_, r.txOn ? 2 : 1));
        if (g < s.gainDb - 0.5 && c.rx_agc == 0) {
            char b[120];
            snprintf(b, sizeof b, "the AD9361 gives at most %.0f dB of gain at %s MHz", g, mhz(f).c_str());
            addNote(err, b);
        }
        return true;
    }
    void closeDevice() override {
        if (!radio_) return;
        {
            std::lock_guard<std::mutex> lk(radio_->ctl);
            if (ownsRx_) {
                radio_->rxOn = false;
                std::string e;
                radio_->rebuild(e);   // the transmitter, if any, goes on alone
            }
            ownsRx_ = false;
        }
        dropStatus(radio_->serial);
        radio_.reset();
    }
    void streamLoop() override {
        std::shared_ptr<OmniRadio> r = radio_;
        if (!r) return;
        auto& a = ochApi();
        conv_.resize(kChunk);
        while (run_) {
            r->drainEvents();
            r->pollRssi();
            if (r->linkLost) return;   // NativeSource closes the radio and opens it again (OCH's handle sends no more commands)
            och::stream* st;
            uint64_t gen;
            int rc;
            size_t got = 0;
            och::metadata m{};
            m.struct_size = sizeof m;
            {
                std::lock_guard<std::mutex> lk(r->rxIo);
                st = r->rx;
                gen = r->streamGen;
                rc = st ? a.recv(st, conv_.data(), kChunk, &got, &m, 100) : och::TIMEOUT;
            }
            if (rc == och::OK) {
                if (m.flags & och::META_HOST_OVERFLOW) hostOverflows_++;
                if (m.flags & och::META_DEVICE_OVERFLOW) devOverflows_++;
                push(conv_.data(), got);
                continue;
            }
            if (!st && !r->rebuilding && !r->rxOn) return;   // the receive stream could not be set up again: NativeSource opens the radio again
            if (rc == och::TIMEOUT || r->rebuilding || gen != r->streamGen) {   // OCH reconfiguring (returns at once), or a new stream on its way
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            const std::string e = a.why(rc);
            if (run_) { fprintf(stderr, "OmniCoreSDR: the receive stream ended: %s\n", e.c_str()); fflush(stderr); }
            return;   // NativeSource opens the radio again
        }
    }

private:
    // (ctl) false when a transmitter runs the shared clock slower than this receiver needs
    bool rateFits(const TuneSettings& s, std::string& err) const {
        if (!radio_ || !radio_->txOn) return true;
        const double need = std::clamp(s.sampleRate > 0 ? s.sampleRate : 10e6, kMinRateHz, kMaxRateHz);
        if (need <= radio_->rate() + 1) return true;
        err = "OmniCoreSDR: receive and transmit share one sample rate; the transmitter runs at " + mhz(radio_->rate()) + " Msps, this needs " + mhz(need) +
              " Msps: stop the transmission first";
        return false;
    }

    std::string serial_;
    std::shared_ptr<OmniRadio> radio_;
    bool ownsRx_ = false;
    std::atomic<uint64_t> hostOverflows_{0}, devOverflows_{0};
};

// ------------------------------------------------------------------ transmit

// The transmit output through och_send (float I/Q, full scale 1.0, quantised to the AD9361's 12 bits by OCH). The gain OnAir shows is the
// AD9361's TX attenuation as a negative number: -89.75 dB (least power) to 0 dB (most), in 0.25 dB steps.
class OmniTx : public TxSink {
public:
    explicit OmniTx(std::string serial) : serial_(std::move(serial)) {}
    ~OmniTx() override { close(); }

    bool open(const TxTune& t, std::string& err) override {
        close();
        bool final = false;
        auto r = openRadio(serial_, err, final);
        if (!r) return false;
        std::lock_guard<std::mutex> lk(r->ctl);
        if (r->txOn) { err = "OmniCoreSDR: the transmit stream is already open"; return false; }
        if (t.channel < 0 || t.channel > 1) { err = "the OmniCoreSDR has two transmit outputs (TX1, TX2)"; return false; }
        double rate;
        if (r->rxOn) {
            if (t.sampleRate > r->rate() + 1) {
                err = "OmniCoreSDR: receive and transmit share one sample rate; the receiver runs at " + mhz(r->rate()) + " Msps, the transmission needs " +
                      mhz(t.sampleRate) + " Msps: stop the receiver first, or use a lower rate";
                return false;
            }
            rate = r->rate();
        } else {
            if (t.sampleRate < kMinRateHz - 1) { err = "OmniCoreSDR: the AD9361 runs from 2.084 Msps up; " + mhz(t.sampleRate) + " Msps is below that"; return false; }
            rate = std::min(t.sampleRate, kMaxRateHz);
        }
        och::config c = r->cfg;
        if (!apply(t, rate, c, err)) return false;
        const och::config before = r->cfg;
        r->cfg = c;
        r->txOn = owns_ = true;
        r->txMask = t.channel == 1 ? 2 : 1;
        if (!r->rebuild(err)) {
            r->txOn = owns_ = false;
            r->cfg = before;
            std::string e2;
            if (r->rxOn) r->rebuild(e2);   // the receiver goes on as before
            return false;
        }
        radio_ = r;
        rate_ = r->rate();
        rateAsked_ = t.sampleRate;
        addNote(err, usb2Note(*r, rate_, r->rxOn ? 2 : 1));
        return true;
    }

    bool write(const cf32* x, size_t n, std::string& err) override {
        if (!radio_) { err = "the OmniCoreSDR is not open for transmitting"; return false; }
        OmniRadio& r = *radio_;
        auto& a = ochApi();
        // the handle sends no more commands: the transmission ends, so that the radio can be opened again
        if (r.linkLost) { err = "the OmniCoreSDR's control link stopped answering: start the transmission again"; return false; }
        size_t i = 0;
        auto progress = std::chrono::steady_clock::now();
        while (i < n) {
            r.drainEvents();
            uint64_t gen;
            int rc;
            size_t done = 0;
            {
                std::lock_guard<std::mutex> lk(r.txIo);
                gen = r.streamGen;
                rc = r.tx ? a.send(r.tx, x + i, n - i, &done, 500) : och::TIMEOUT;
            }
            i += done;
            if (done) progress = std::chrono::steady_clock::now();
            if (rc == och::OK) continue;
            if (rc == och::TIMEOUT || r.rebuilding || gen != r.streamGen) {   // a full queue, OCH reconfiguring (returns at once), or a new stream on its way
                if (std::chrono::steady_clock::now() - progress > std::chrono::seconds(20)) { err = "the OmniCoreSDR stopped taking samples"; return false; }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            err = "the OmniCoreSDR stopped taking samples: " + a.why(rc);
            return false;
        }
        return true;
    }

    bool retune(const TxTune& t, std::string& err) override {
        if (!radio_) { err = "the OmniCoreSDR is not open for transmitting"; return false; }
        OmniRadio& r = *radio_;
        std::lock_guard<std::mutex> lk(r.ctl);
        if (std::fabs(t.sampleRate - rateAsked_) > 1) { err = "OmniCoreSDR: the transmit sample rate changes only when the transmission starts again"; return false; }
        if (t.channel < 0 || t.channel > 1) { err = "the OmniCoreSDR has two transmit outputs (TX1, TX2)"; return false; }
        och::config c = r.cfg;
        if (!apply(t, r.rate(), c, err)) return false;
        const och::config before = r.cfg;
        r.cfg = c;
        const uint32_t mask = t.channel == 1 ? 2 : 1;
        bool ok;
        if (mask != r.txMask) { r.txMask = mask; ok = r.rebuild(err); }
        else ok = r.configure(err);
        if (!ok) r.cfg = before;
        return ok;
    }

    void close() override {
        if (!radio_) return;
        {
            std::lock_guard<std::mutex> lk(radio_->ctl);
            if (owns_) {
                {   // the samples already queued go out (OCH pads the last USB block with silence)
                    std::lock_guard<std::mutex> io(radio_->txIo);
                    if (radio_->tx) ochApi().flush(radio_->tx, 300);
                }
                radio_->txOn = false;
                std::string e;
                radio_->rebuild(e);   // least power on both outputs; the receiver, if any, goes on alone
            }
            owns_ = false;
        }
        radio_.reset();
    }
    double sampleRate() const override { return rate_; }
    bool fullDuplex() const override { return true; }
    std::string name() const override { return "OmniCoreSDR"; }

private:
    // the transmit part of the configuration
    static bool apply(const TxTune& t, double rate, och::config& c, std::string& err) {
        TuneSettings ts;
        ts.radio = t.radio;
        const double hz = ppmCorrectedHz(txRadioHz(t), radioPpm(ts));
        if (hz < kMinFreqHz || hz > kMaxFreqHz) {
            err = "OmniCoreSDR: the AD9361 transmits " + mhz(kMinFreqHz) + " to " + mhz(kMaxFreqHz) + " MHz; " + mhz(txRadioHz(t)) + " MHz cannot be set";
            return false;
        }
        c.sample_rate = (uint32_t)std::llround(rate);
        c.tx_frequency = (uint64_t)std::llround(hz);
        c.tx_bandwidth = filterHz(radioNumber(ts, "txbw", 0), t.bandwidthHz, rate);
        const double atten = std::clamp(std::round(-t.gainDb * 4) / 4, 0.0, kMaxAttenDb);
        c.tx_attenuation[0] = c.tx_attenuation[1] = atten;
        return true;
    }

    std::string serial_;
    std::shared_ptr<OmniRadio> radio_;
    bool owns_ = false;
    double rate_ = 0, rateAsked_ = 0;
};

// ------------------------------------------------------------------ the list

void listOmnicore(std::vector<DeviceInfo>& out) {
    auto& a = ochApi();
    if (!a.ok) return;
    std::vector<och::device_info> v;
    if (usbFdPlatform()) {
        if (!a.open_android_fd) return;
        for (const auto& fd : usbFdList("omnicore")) {
            och::device_info i{};
            i.struct_size = sizeof i;
            snprintf(i.serial, sizeof i.serial, "%s", fd.serial.c_str());
            snprintf(i.name, sizeof i.name, "OmniCore SDR");
            i.max_sample_rate = 61440000; // radio capability; the phone's transport may sustain less
            v.push_back(i);
        }
    } else {
        size_t n = 0;
        if (a.list(nullptr, 0, &n) != och::OK || n == 0) return;
        v.resize(std::min<size_t>(n, 16));
        for (auto& i : v) { i = {}; i.struct_size = sizeof i; }
        if (a.list(v.data(), v.size(), &n) != och::OK) return;
        v.resize(std::min(n, v.size()));
    }
    static size_t logged = (size_t)-1;
    if (v.size() != logged) {
        logged = v.size();
        fprintf(stderr, "OmniCore Host: %zu OmniCoreSDR radio(s)\n", v.size());
        fflush(stderr);
    }
    for (const auto& i : v) {
        DeviceInfo d;
        d.kind = DeviceInfo::Native;
        d.board = "omnicore";
        d.serial = trimmed(i.serial, sizeof i.serial);
        d.nativeArgs = d.serial;
        const std::string nm = trimmed(i.name, sizeof i.name);
        d.name = (nm.empty() ? std::string("OmniCoreSDR") : nm) + (d.serial.empty() ? "" : " " + d.serial) + " (native, experimental)";
        d.minRateHz = kMinRateHz; d.maxRateHz = i.max_sample_rate ? std::min<double>(i.max_sample_rate, kMaxRateHz) : kMaxRateHz;
        if (i.usb_speed_mbps == 480) d.steadyRateHz = kUsb2RateHz;
        if (usbFdPlatform()) d.steadyRateHz = 4e6; // start conservatively until real-phone throughput is measured
        d.gainMinDb = 0; d.gainMaxDb = 73;
        d.gainNote = "the AD9361's RX gain: at most 73 dB to 1.3 GHz, 71 dB to 4 GHz, 62 dB above";
        d.minFreqHz = kMinFreqHz; d.maxFreqHz = kMaxFreqHz;
        d.canTransmit = true;
        d.txFullDuplex = true;
        d.txChannels = 2;
        d.txMinRateHz = kMinRateHz; d.txMaxRateHz = kMaxRateHz;
        d.txGainMinDb = -kMaxAttenDb; d.txGainMaxDb = 0;
        d.txGainNote = "the AD9361's TX attenuation shown as a gain: -89.75 dB (least power) to 0 dB (most), 0.25 dB steps";
        d.settings = omniSettings();
        out.push_back(d);
    }
}

std::unique_ptr<IqSource> makeOmnicore(const DeviceInfo& d) { return std::make_unique<OmniSource>(d.nativeArgs); }
std::unique_ptr<TxSink> makeOmnicoreTx(const DeviceInfo& d) { return std::make_unique<OmniTx>(d.nativeArgs); }

} // namespace native

OmniCoreStatus omnicoreStatus(const std::string& serial) {
    std::lock_guard<std::mutex> lk(native::gStatusMu);
    const auto it = native::gStatus.find(serial);
    return it == native::gStatus.end() ? OmniCoreStatus() : it->second;
}

} // namespace dect2
