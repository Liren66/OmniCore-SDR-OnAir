// Opt-in hardware acceptance tool for the OmniCoreSDR. Uses OnAir's actual source and transmit sink (source_omnicore.cpp).
// omnicore_probe [--freq Hz] [--rate Hz] [--gain dB] [--secs N] [--matrix] [--capture file.cf32]
//                [--tx] [--txfreq Hz] [--txgain dB] [--txch 0|1] [--radio key=value ...]
// Without --tx nothing is transmitted. --tx sends a tone 100 kHz above --txfreq at --txgain (default -70 dB, i.e. 70 dB attenuation):
// connect a dummy load or an attenuator, and use a frequency you may transmit on. Close och.exe and other programs using the radio first.
#include "../core/src/source/native_common.h"
#include "dect2/source/omnicore.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
using namespace dect2;

namespace dect2 { namespace native {
void listOmnicore(std::vector<DeviceInfo>& out);
std::unique_ptr<IqSource> makeOmnicore(const DeviceInfo& d);
std::unique_ptr<TxSink> makeOmnicoreTx(const DeviceInfo& d);
} }

static std::string gSerial;   // the radio in use, for its RSSI

// toneDbfs: the level at +100 kHz from the centre (where --tx puts its tone when RX and TX share the frequency)
static bool receive(IqSource& source, IqRing& ring, double seconds, std::ofstream* capture, double* dcDbfs = nullptr, double* toneDbfs = nullptr) {
    std::vector<cf32> b(65536);
    ring.clear(); ring.resetDropped();
    // the first 50 ms after a change may still be the PLL settling or the old setting
    for (const auto t0 = std::chrono::steady_clock::now(); std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(50);)
        if (!ring.read(b.data(), b.size())) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ring.resetDropped();
    uint64_t total = 0, bad = 0;
    double energy = 0, peak = 0;
    std::complex<double> sum = 0, tone = 0;
    const std::complex<double> toneStep = std::polar(1.0, -2 * M_PI * 100e3 / source.sampleRate());
    std::complex<double> lo = 1;   // a rotating phasor: std::polar per sample cannot keep up with 61.44 Msps
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < seconds) {
        const size_t n = ring.read(b.data(), b.size());
        lo /= std::abs(lo);   // keep the phasor's length at 1
        for (size_t i = 0; i < n; ++i) {
            const double p = std::norm(b[i]);
            if (!std::isfinite(p)) ++bad;
            else { energy += p; peak = std::max(peak, p); sum += std::complex<double>(b[i]); tone += std::complex<double>(b[i]) * lo; }
            lo *= toneStep;
        }
        total += n;
        if (capture && n) capture->write(reinterpret_cast<const char*>(b.data()), n * sizeof(cf32));
        if (!n) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double measured = total / elapsed, expected = source.sampleRate();
    const double dc = total ? 20 * std::log10(std::abs(sum) / total + 1e-12) : -999;
    if (dcDbfs) *dcDbfs = dc;
    const double td = total ? 20 * std::log10(std::abs(tone) / total + 1e-12) : -999;
    if (toneDbfs) *toneDbfs = td;
    const bool ok = total > 0 && !bad && energy > 0 && ring.dropped() == 0 && measured > expected * 0.90 && measured < expected * 1.10 && (!capture || capture->good());
    std::cout << std::fixed << std::setprecision(3) << (ok ? "PASS" : "FAIL") << " rate=" << expected << " measured=" << measured
              << " samples=" << total << " dropped=" << ring.dropped() << " bad=" << bad
              << " rms_dbfs=" << (total && energy > 0 ? 10 * std::log10(energy / total) : -999) << " dc_dbfs=" << dc << " tone_dbfs=" << td << " peak=" << std::sqrt(peak);
    const OmniCoreStatus rs = omnicoreStatus(gSerial);
    if (rs.rssiKnown) std::cout << " rssi=" << (rs.valid[0] ? std::to_string(rs.rssiDb[0]) : "--") << "/" << (rs.valid[1] ? std::to_string(rs.rssiDb[1]) : "--") << " dB";
    else if (rs.receiving && !rs.note.empty()) std::cout << " rssi: " << rs.note;
    const std::string note = source.statusNote();
    if (!note.empty()) std::cout << " [" << note << "]";
    std::cout << '\n';
    return ok;
}

static bool retune(IqSource& s, const TuneSettings& t, const char* what) {
    std::string err;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = s.retune(t, err);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::cout << what << ": " << (ok ? "ok" : "REFUSED") << " in " << std::fixed << std::setprecision(1) << ms << " ms" << (err.empty() ? "" : " (" + err + ")") << '\n';
    return ok;
}

// Sends a tone for seconds while the receiver keeps running; checks that the radio paces the writes at its rate
static bool transmit(TxSink& tx, double seconds) {
    const double rate = tx.sampleRate();
    std::vector<cf32> b(16384);
    double ph = 0;
    const double step = 2 * M_PI * 100e3 / rate;
    uint64_t sent = 0;
    std::string err;
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < seconds) {
        // a rotating phasor (one complex multiply a sample; cos/sin per sample cannot keep up with 20 Msps and more)
        std::complex<double> z = std::polar(0.5, ph);
        const std::complex<double> w = std::polar(1.0, step);
        for (auto& x : b) { x = cf32((float)z.real(), (float)z.imag()); z *= w; }
        ph = std::fmod(ph + step * (double)b.size(), 2 * M_PI);
        if (!tx.write(b.data(), b.size(), err)) { std::cout << "FAIL tx write: " << err << '\n'; return false; }
        sent += b.size();
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double measured = sent / elapsed;
    // the driver keeps OCH's transmit buffers to ~0.15 s, so the radio paces the writes from the start
    const bool ok = seconds < 2 || (measured > rate * 0.9 && measured < rate * 1.2);
    std::cout << std::fixed << std::setprecision(3) << (ok ? "PASS" : "FAIL") << " tx rate=" << rate << " written=" << measured << " samples/s\n";
    return ok;
}

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    double seconds = 3, txRetuneStep = 1e6; bool matrix = false, txOn = false;
    TuneSettings t; t.centerHz = 100e6; t.sampleRate = 4e6; t.bandwidthMhz = 0.2; t.gainDb = 40;
    TxTune tt; tt.centerHz = 433.92e6; tt.gainDb = -70;
    std::string capturePath, serial;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            std::cout << "OmniCore-SDR-OnAir hardware probe\n"
                      << "Usage: omnicore_probe [--serial ID] [--rate Hz] [--freq Hz] [--gain dB] [--secs N]\n"
                      << "       [--capture file] [--radio key=value] [--matrix]\n"
                      << "       [--tx --txfreq Hz --txgain dB --txch N]\n"
                      << "       [--tx-retune-step Hz] (default 1000000; use 0 to keep TX frequency)\n"
                      << "Without --help this opens the first OmniCore device. --matrix retunes it.\n"
                      << "TX is disabled unless --tx is explicitly supplied.\n";
            return 0;
        }
        if (a == "--matrix") { matrix = true; continue; }
        if (a == "--tx") { txOn = true; continue; }
        if (i + 1 == argc) { std::cerr << "missing value for " << a << '\n'; return 2; }
        const std::string v = argv[++i];
        if (a == "--rate") t.sampleRate = std::stod(v);
        else if (a == "--freq") t.centerHz = std::stod(v);
        else if (a == "--gain") t.gainDb = std::stod(v);
        else if (a == "--secs") seconds = std::stod(v);
        else if (a == "--capture") capturePath = v;
        else if (a == "--serial") serial = v;
        else if (a == "--txfreq") tt.centerHz = std::stod(v);
        else if (a == "--txgain") tt.gainDb = std::stod(v);
        else if (a == "--tx-retune-step") txRetuneStep = std::stod(v);
        else if (a == "--txch") tt.channel = std::stoi(v);
        else if (a == "--radio") { const size_t eq = v.find('='); if (eq != std::string::npos) t.radio[v.substr(0, eq)] = v.substr(eq + 1); }   // a radio setting, e.g. --radio rssi=0
        else { std::cerr << "unknown option " << a << '\n'; return 2; }
    }
    if (seconds < 1 || seconds > 3600) return 2;
    std::vector<DeviceInfo> devices;
    native::listOmnicore(devices);
    for (size_t i = 0; i < devices.size(); ++i) std::cout << i << ": " << devices[i].name << '\n';
    if (devices.empty()) { std::cerr << "no OmniCoreSDR: install OmniCore Host, check the driver (och-driver check) and close och.exe\n"; return 1; }
    const auto selected = serial.empty() ? devices.begin() : std::find_if(devices.begin(), devices.end(), [&](const DeviceInfo& d) { return d.serial == serial; });
    if (selected == devices.end()) { std::cerr << "OmniCoreSDR serial not found: " << serial << '\n'; return 2; }
    if (serial.empty() && devices.size() != 1) { std::cerr << "multiple OmniCoreSDR devices: select one with --serial\n"; return 2; }
    const DeviceInfo& dev = *selected;
    gSerial = dev.serial;
    std::ofstream capture;
    if (!capturePath.empty()) { capture.open(capturePath, std::ios::binary); if (!capture) return 2; }
    auto source = native::makeOmnicore(dev);
    IqRing ring(1 << 23);
    std::string err;
    std::atomic<int> failures{0};
    if (!source->start(t, ring, err)) { std::cerr << err << '\n'; return 1; }
    if (!err.empty()) std::cerr << err << '\n';
    if (!receive(*source, ring, seconds, capture.is_open() ? &capture : nullptr)) ++failures;
    if (matrix) {
        for (double rate : {2.084e6, 2.5e6, 5e6, 10e6, 11.2e6, 20e6, 30.72e6}) {
            t.sampleRate = rate;
            if (!retune(*source, t, ("rate " + std::to_string(rate)).c_str()) || !receive(*source, ring, 2, nullptr)) ++failures;
        }
        t.sampleRate = 4e6;
        retune(*source, t, "rate 4e6");
        for (double hz : {70e6, 100e6, 433.92e6, 915e6, 1.299e9, 1.575e9, 2.45e9, 4.1e9, 5.8e9, 6e9}) {
            t.centerHz = hz;
            for (double gain : {0.0, 40.0, 73.0}) {
                t.gainDb = gain;
                if (!retune(*source, t, ("tune " + std::to_string(hz) + " gain " + std::to_string(gain)).c_str()) || !receive(*source, ring, 1, nullptr)) ++failures;
            }
        }
        t.centerHz = 100e6; t.gainDb = 40;
        retune(*source, t, "back to 100 MHz");
        // a frequency OCH cannot tune must be refused without stopping the stream
        { TuneSettings bad = t; bad.centerHz = 50e6; std::string e; if (source->retune(bad, e)) ++failures; std::cout << "50 MHz refused: " << e << '\n'; }
        if (!receive(*source, ring, 1, nullptr)) ++failures;
        // the radio settings
        double dcOn = 0, dcOff = 0;
        t.radio["dccorr"] = "0";
        if (!retune(*source, t, "DC correction off") || !receive(*source, ring, 2, nullptr, &dcOff)) ++failures;
        t.radio["dccorr"] = "1";
        if (!retune(*source, t, "DC correction on") || !receive(*source, ring, 2, nullptr, &dcOn)) ++failures;
        std::cout << "DC level: correction off " << dcOff << " dBFS, on " << dcOn << " dBFS\n";
        for (const char* v : {"0", "1"}) {
            t.radio["iqcorr"] = v;
            if (!retune(*source, t, (std::string("IQ correction ") + v).c_str()) || !receive(*source, ring, 1, nullptr)) ++failures;
        }
        for (const char* bw : {"0.2", "1", "20", "56", "0"}) {
            t.radio["rxbw"] = bw;
            if (!retune(*source, t, (std::string("RX bandwidth ") + bw + " MHz").c_str()) || !receive(*source, ring, 1, nullptr)) ++failures;
        }
        for (const char* gm : {"slow", "fast", "hybrid", "manual"}) {
            t.radio["gainmode"] = gm;
            if (!retune(*source, t, (std::string("gain mode ") + gm).c_str()) || !receive(*source, ring, 1, nullptr)) ++failures;
        }
        for (const char* port : {"rx2", "rx1"}) {
            t.radio["rxport"] = port;
            if (!retune(*source, t, (std::string("input ") + port).c_str()) || !receive(*source, ring, 1, nullptr)) ++failures;
        }
        t.radio.clear();
    }
    if (txOn) {
        auto tx = native::makeOmnicoreTx(dev);
        tt.sampleRate = source->sampleRate();
        tt.radio = t.radio;
        std::cout << "TX on " << tt.centerHz << " Hz (+100 kHz tone), gain " << tt.gainDb << " dB, output TX" << tt.channel + 1 << '\n';
        if (!tx->open(tt, err)) { std::cout << "FAIL tx open: " << err << '\n'; ++failures; }
        else {
            if (!err.empty()) std::cout << "tx: " << err << '\n';
            // the receiver goes on while transmitting (full duplex), after the short gap of setting up both streams
            double toneOn = -999;
            std::thread rxCheck([&] { if (!receive(*source, ring, seconds, nullptr, nullptr, &toneOn)) ++failures; });
            if (!transmit(*tx, seconds + 1)) ++failures;
            rxCheck.join();
            TxTune t2 = tt; t2.gainDb = tt.gainDb - 10; t2.centerHz = tt.centerHz + txRetuneStep;
            std::string e;
            const bool rt = tx->retune(t2, e);
            std::cout << "tx retune " << txRetuneStep << " Hz, -10 dB: " << (rt ? "ok" : "REFUSED " + e) << '\n';
            if (!rt || !transmit(*tx, 2)) ++failures;
            tx->close();
            std::cout << "tx closed\n";
            double toneOff = -999;
            if (!receive(*source, ring, 2, nullptr, nullptr, &toneOff)) ++failures;
            std::cout << "tone at +100 kHz while transmitting " << toneOn << " dBFS, after " << toneOff << " dBFS\n";
        }
    }
    source->stop();
    // a fresh start proves that stop released the radio
    for (int i = 0; i < 3; ++i) {
        if (!source->start(t, ring, err)) { std::cerr << "restart: " << err << '\n'; ++failures; break; }
        if (!receive(*source, ring, 1, nullptr)) ++failures;
        source->stop();
    }
    std::cout << "hardware probe: " << failures << " failure(s)\n";
    return failures.load() ? 1 : 0;
}
