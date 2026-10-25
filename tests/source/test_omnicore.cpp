// The OmniCoreSDR driver (source_omnicore.cpp) against a fake OmniCore Host library (omnicore_fake.cpp): the listing, receive, live retunes
// without new streams, the radio settings (analog bandwidths, DC / IQ correction, gain mode, RX input), refused frequencies that must not stop
// the stream, receive and transmit at once on one handle (the masks are set up again when a direction joins or leaves), the shared sample
// rate, the RX RSSI the interface shows, and the radio being released after stop. test_omnicore <path of the fake library>
#include "../../core/src/source/native_common.h"
#include "dect2/source/omnicore.h"
#include <cstdio>

using namespace dect2;

namespace dect2 { namespace native {
void listOmnicore(std::vector<DeviceInfo>& out);
std::unique_ptr<IqSource> makeOmnicore(const DeviceInfo& d);
std::unique_ptr<TxSink> makeOmnicoreTx(const DeviceInfo& d);
} }

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

typedef double (*GetFn)(const char*);
typedef void (*SetFn)(const char*, double);
static GetFn fget;
static SetFn fset;

static bool loadFake(const std::string& path) {
#ifdef _WIN32
    HMODULE h = LoadLibraryExA(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) return false;
    fget = (GetFn)(void*)GetProcAddress(h, "fake_get");
    fset = (SetFn)(void*)GetProcAddress(h, "fake_set");
#else
    void* h = dlopen(path.c_str(), RTLD_NOW);
    if (!h) return false;
    fget = (GetFn)dlsym(h, "fake_get");
    fset = (SetFn)dlsym(h, "fake_set");
#endif
    return fget && fset;
}

// the interface's status of the fake radio once pred holds (or after 2 s)
template <class P> static OmniCoreStatus statusWhen(P pred) {
    for (int k = 0; k < 100; k++) {
        const OmniCoreStatus st = omnicoreStatus("FAKE0001");
        if (pred(st)) return st;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return omnicoreStatus("FAKE0001");
}

struct Got { size_t n = 0; double dc = 0, rms = 0; };
// what arrives in the ring within ms (after dropping what was there)
static Got receive(IqRing& ring, int ms) {
    std::vector<cf32> b(65536);
    ring.clear();
    Got g;
    std::complex<double> sum = 0;
    double e = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        const size_t n = ring.read(b.data(), b.size());
        for (size_t i = 0; i < n; i++) { sum += std::complex<double>(b[i]); e += std::norm(b[i]); }
        g.n += n;
        if (!n) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (g.n) { g.dc = std::abs(sum) / g.n; g.rms = std::sqrt(e / g.n); }
    return g;
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: test_omnicore <fake omnicore library>\n"); return 2; }
    std::string lib = argv[1];
    const size_t cut = lib.find_last_of("/\\");
    const std::string dir = cut == std::string::npos ? "." : lib.substr(0, cut);
#ifdef _WIN32
    _putenv_s("DECT2_NATIVE_LIBDIR", dir.c_str());
#else
    setenv("DECT2_NATIVE_LIBDIR", dir.c_str(), 1);
#endif
    if (!loadFake(lib)) { printf("cannot load the fake library %s\n", lib.c_str()); return 2; }

    // ---- the listing
    std::vector<DeviceInfo> list;
    native::listOmnicore(list);
    CHECK(list.size() == 1);
    if (list.size() != 1) return 1;
    const DeviceInfo d = list[0];
    CHECK(d.board == "omnicore" && d.kind == DeviceInfo::Native && d.serial == "FAKE0001" && d.nativeArgs == "FAKE0001");
    CHECK(d.canTransmit && d.txFullDuplex && d.txChannels == 2);
    CHECK(d.minFreqHz == 70e6 && d.maxFreqHz == 6e9 && d.maxRateHz == 61.44e6 && d.minRateHz > 2.08e6 && d.minRateHz < 2.09e6);
    CHECK(d.txGainMinDb == -89.75 && d.txGainMaxDb == 0);
    auto has = [&](const char* k) { for (const auto& s : d.settings) if (s.key == k) return true; return false; };
    CHECK(has("rxbw") && has("txbw") && has("dccorr") && has("iqcorr") && has("gainmode") && has("rxport") && has("ppm"));

    // ---- receive
    auto rx = native::makeOmnicore(d);
    IqRing ring(1 << 22);
    TuneSettings t;
    t.centerHz = 433.92e6; t.sampleRate = 4e6; t.gainDb = 40.4;
    std::string err;
    CHECK(rx->start(t, ring, err));
    CHECK(fget("open") == 1 && fget("rx_streams") == 1 && fget("tx_streams") == 0);
    CHECK(fget("rx_mask") == 1 && fget("tx_mask") == 0 && fget("rx_agc") == 0);
    CHECK(fget("rate") == 4e6 && fget("rx_freq") == 433.92e6 && fget("rx_gain") == 40);
    CHECK(fget("rx_bw") == 3.8e6);   // automatic: 95 % of the rate
    CHECK(fget("tx_atten") == 89.75);
    CHECK(rx->sampleRate() == 4e6);
    Got g = receive(ring, 300);
    CHECK(g.n > 4e6 * 0.3 * 0.7 && g.n < 4e6 * 0.3 * 1.3);
    CHECK(std::fabs(g.rms - 0.5) < 0.01 && g.dc < 0.02);

    // ---- frequency and gain change while streaming, without a new stream
    t.centerHz = 915e6; t.gainDb = 20;
    err.clear();
    CHECK(rx->retune(t, err));
    CHECK(fget("rx_freq") == 915e6 && fget("rx_gain") == 20 && fget("rx_streams") == 1);
    // above 4 GHz the AD9361 gives at most 62 dB: the gain is held to it (a refused gain would stop the streams) with a note
    t.centerHz = 5.8e9; t.gainDb = 73;
    err.clear();
    CHECK(rx->retune(t, err));
    CHECK(fget("rx_gain") == 62 && err.find("62 dB") != std::string::npos && fget("refused") == 0);
    t.centerHz = 915e6; t.gainDb = 30;
    rx->retune(t, err);
    // a frequency OCH cannot tune is refused before it is asked, and the stream goes on
    TuneSettings bad = t;
    bad.centerHz = 50e6;
    err.clear();
    CHECK(!rx->retune(bad, err));
    CHECK(err.find("70") != std::string::npos && fget("refused") == 0 && fget("rx_freq") == 915e6);
    CHECK(receive(ring, 100).n > 0);

    // ---- the sample rate changes in the running stream (OCH pauses it); OCH's nearest rate is what the receiver gets
    t.sampleRate = 11.2e6;
    err.clear();
    CHECK(rx->retune(t, err));
    CHECK(fget("rate") == 11200011 && rx->sampleRate() == 11200011 && fget("rx_streams") == 1);
    CHECK(fget("rx_bw") == std::llround(11.2e6 * 0.95));   // from the rate asked for (OCH's applied rate is known only afterwards)
    t.sampleRate = 1e6;   // below what the AD9361 runs without its FIR: the lowest rate it has (OnAir resamples)
    err.clear();
    CHECK(rx->retune(t, err));
    CHECK(fget("rate") == 2084000 && fget("rx_streams") == 1);
    t.sampleRate = 4e6;
    rx->retune(t, err);

    // ---- analog bandwidth: the radio setting, else the channel's filter, else automatic
    t.basebandFilterHz = 2e6;
    rx->retune(t, err);
    CHECK(fget("rx_bw") == 2e6);
    t.radio["rxbw"] = "5";
    rx->retune(t, err);
    CHECK(fget("rx_bw") == 5e6);
    t.radio["rxbw"] = "0.05";   // held to the AD9361's 0.2 MHz
    rx->retune(t, err);
    CHECK(fget("rx_bw") == 0.2e6);
    t.radio.erase("rxbw"); t.basebandFilterHz = 0;
    rx->retune(t, err);
    CHECK(fget("rx_bw") == 3.8e6 && fget("rx_streams") == 1);

    // ---- DC and IQ correction
    CHECK(fget("dc") == 1 && fget("iq") == 1);
    t.radio["dccorr"] = "0";
    CHECK(rx->retune(t, err));
    CHECK(fget("dc") == 0 && fget("iq") == 1);
    CHECK(receive(ring, 150).dc > 0.1);
    t.radio["dccorr"] = "1"; t.radio["iqcorr"] = "0";
    CHECK(rx->retune(t, err));
    CHECK(fget("dc") == 1 && fget("iq") == 0);
    CHECK(receive(ring, 150).dc < 0.02);
    t.radio.erase("dccorr"); t.radio.erase("iqcorr");
    rx->retune(t, err);
    CHECK(fget("iq") == 1);

    // ---- gain mode
    t.radio["gainmode"] = "fast";
    rx->retune(t, err);
    CHECK(fget("rx_agc") == 1);
    t.radio["gainmode"] = "hybrid";
    rx->retune(t, err);
    CHECK(fget("rx_agc") == 3);
    t.radio.erase("gainmode");
    rx->retune(t, err);
    CHECK(fget("rx_agc") == 0);

    // ---- RX RSSI: switched on by default (the SDK starts with it off), read while receiving without touching the stream
    {
        CHECK(fget("rssi_on") == 1);
        const double streams = fget("rx_streams");
        OmniCoreStatus st = statusWhen([](const OmniCoreStatus& x) { return x.rssiKnown; });
        CHECK(st.receiving && st.rssiOn && st.rssiKnown && st.valid[0] && st.valid[1] && st.input == 0 && st.note.empty());
        CHECK(std::fabs(st.rssiDb[0] - (-90 + fget("rx_gain") / 2)) < 1e-9 && std::fabs(st.rssiDb[1] - (st.rssiDb[0] - 1.5)) < 1e-9);
        // a new gain shows up in the next reading (every 500 ms)
        t.gainDb = 50;
        CHECK(rx->retune(t, err));
        st = statusWhen([](const OmniCoreStatus& x) { return x.rssiKnown && std::fabs(x.rssiDb[0] - (-65)) < 1e-9; });
        CHECK(std::fabs(st.rssiDb[0] - (-65)) < 1e-9);
        CHECK(fget("rx_streams") == streams && fget("refused") == 0);
        CHECK(receive(ring, 100).n > 0);
        // off: the radio stops reporting and the driver stops asking
        t.radio["rssi"] = "0";
        CHECK(rx->retune(t, err));
        CHECK(fget("rssi_on") == 0);
        st = statusWhen([](const OmniCoreStatus& x) { return !x.rssiOn; });
        CHECK(st.receiving && !st.rssiOn && !st.rssiKnown);
        const double reads = fget("rssi_reads");
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        CHECK(fget("rssi_reads") == reads);
        t.radio.erase("rssi");
        CHECK(rx->retune(t, err));
        CHECK(fget("rssi_on") == 1);
        CHECK(statusWhen([](const OmniCoreStatus& x) { return x.rssiKnown; }).rssiKnown);
        t.gainDb = 30;
        rx->retune(t, err);
    }

    // ---- the other input: OCH needs the streams set up again for another channel mask
    t.radio["rxport"] = "rx2";
    err.clear();
    CHECK(rx->retune(t, err));
    CHECK(fget("rx_mask") == 2 && fget("rx_streams") == 2);
    CHECK(statusWhen([](const OmniCoreStatus& x) { return x.input == 1; }).input == 1);   // the RSSI status says which input is in use
    CHECK(receive(ring, 150).n > 0);
    t.radio.erase("rxport");
    rx->retune(t, err);
    CHECK(fget("rx_mask") == 1 && fget("rx_streams") == 3);
    CHECK(receive(ring, 150).n > 0);

    // ---- transmit while receiving, on the same handle
    auto tx = native::makeOmnicoreTx(d);
    TxTune tt;
    tt.centerHz = 433.92e6; tt.sampleRate = 8e6; tt.gainDb = -30;
    err.clear();
    CHECK(!tx->open(tt, err));   // more than the receiver's shared clock runs at
    CHECK(err.find("share one sample rate") != std::string::npos && fget("tx_streams") == 0);
    CHECK(receive(ring, 100).n > 0);
    tt.sampleRate = 2.5e6;   // below the receiver's rate: TX runs at the shared 4 Msps
    tt.gainDb = -30.1;
    tt.radio["txbw"] = "1.5";
    err.clear();
    CHECK(tx->open(tt, err));
    CHECK(tx->sampleRate() == 4e6 && tx->fullDuplex());
    CHECK(fget("rx_mask") == 1 && fget("tx_mask") == 1 && fget("tx_streams") == 1 && fget("rx_streams") == 4);
    CHECK(fget("tx_freq") == 433.92e6 && fget("tx_atten") == 30 && fget("tx_bw") == 1.5e6);
    {
        std::vector<cf32> tone(40000, cf32(0.25f, 0.f));
        const double before = fget("tx_samples");
        std::thread rxCheck([&] { CHECK(receive(ring, 200).n > 4e6 * 0.2 * 0.5); });
        std::string we;
        for (int k = 0; k < 10; k++) CHECK(tx->write(tone.data(), tone.size(), we));
        rxCheck.join();
        CHECK(fget("tx_samples") - before >= 400000 && std::fabs(fget("tx_peak") - 0.25) < 1e-6);
    }
    // the receiver cannot raise the shared rate while TX runs; it may follow it
    t.sampleRate = 8e6;
    err.clear();
    CHECK(!rx->retune(t, err));
    CHECK(fget("rate") == 4e6);
    t.sampleRate = 2e6;
    err.clear();
    CHECK(rx->retune(t, err));
    CHECK(fget("rate") == 4e6 && rx->sampleRate() == 4e6);
    // TX retune: frequency, gain, the other output
    TxTune t2 = tt;
    t2.centerHz = 434.92e6; t2.gainDb = -40;
    err.clear();
    CHECK(tx->retune(t2, err));
    CHECK(fget("tx_freq") == 434.92e6 && fget("tx_atten") == 40 && fget("tx_streams") == 1);
    t2.channel = 1;
    CHECK(tx->retune(t2, err));
    CHECK(fget("tx_mask") == 2 && fget("tx_streams") == 2);
    t2.sampleRate = 5e6;
    err.clear();
    CHECK(!tx->retune(t2, err));
    TxTune tbad = tt;
    tbad.centerHz = 7e9;
    err.clear();
    CHECK(!tx->retune(tbad, err));
    CHECK(fget("refused") == 0);
    // TX gone: the receiver goes on alone, both outputs at the least power
    tx->close();
    CHECK(fget("tx_mask") == 0 && fget("tx_atten") == 89.75 && fget("flushes") >= 1);
    CHECK(receive(ring, 150).n > 0);
    rx->stop();
    CHECK(fget("open") == 0);
    CHECK(!omnicoreStatus("FAKE0001").receiving);   // no status of a radio that does not receive here

    // ---- transmit alone, then a receiver joins and leaves
    {
        TxTune a;
        a.centerHz = 1e9; a.sampleRate = 1e6; a.gainDb = -60;
        err.clear();
        CHECK(!tx->open(a, err));   // below 2.084 Msps with nothing else running
        a.sampleRate = 10e6;
        err.clear();
        CHECK(tx->open(a, err));
        CHECK(fget("open") == 1 && fget("rx_mask") == 0 && fget("tx_mask") == 1 && fget("rate") == 10e6);
        TuneSettings r;
        r.centerHz = 100e6; r.sampleRate = 2.048e6; r.gainDb = 30;
        err.clear();
        CHECK(rx->start(r, ring, err));
        CHECK(fget("rx_mask") == 1 && fget("tx_mask") == 1 && rx->sampleRate() == 10e6);
        CHECK(receive(ring, 150).n > 0);
        std::vector<cf32> z(20000);
        std::string we;
        CHECK(tx->write(z.data(), z.size(), we));
        rx->stop();
        CHECK(fget("rx_mask") == 0 && fget("tx_mask") == 1 && fget("open") == 1);
        CHECK(tx->write(z.data(), z.size(), we));
        tx->close();
        CHECK(fget("open") == 0);
    }

    // ---- an RX DC / IQ control the firmware does not have: a note, the stream runs
    fset("nocorr", 1);
    {
        TuneSettings r;
        r.centerHz = 100e6; r.sampleRate = 4e6; r.radio["dccorr"] = "0";
        err.clear();
        CHECK(rx->start(r, ring, err));
        CHECK(err.find("rx_correction") != std::string::npos);
        CHECK(receive(ring, 100).n > 0);
        rx->stop();
    }
    fset("nocorr", 0);

    // ---- an SDK without the RSSI switch (before rssi=1): it always reports, the switch is refused, the reading still shows
    fset("oldsdk", 1);
    {
        TuneSettings r;
        r.centerHz = 100e6; r.sampleRate = 4e6; r.gainDb = 20;
        err.clear();
        CHECK(rx->start(r, ring, err));
        const OmniCoreStatus st = statusWhen([](const OmniCoreStatus& x) { return x.rssiKnown; });
        CHECK(st.rssiKnown && st.valid[0] && std::fabs(st.rssiDb[0] - (-80)) < 1e-9);
        rx->stop();
    }
    fset("oldsdk", 0);

    // ---- a control command that times out leaves OCH's handle unable to send more: the radio is opened again
    {
        TuneSettings r;
        r.centerHz = 100e6; r.sampleRate = 4e6; r.gainDb = 20;
        // during the open (the 2nd control call: the first RX DC correction): start() opens the radio again and succeeds
        double opens = fget("opens");
        fset("ctlfailin", 2);
        err.clear();
        CHECK(rx->start(r, ring, err));
        CHECK(fget("ctl_timeouts") == 1 && fget("opens") == opens + 2);
        CHECK(receive(ring, 100).n > 0);
        // while receiving (the next RSSI reading): the stream thread opens the radio again by itself, and tuning works afterwards
        opens = fget("opens");
        fset("ctlfailin", 1);
        for (int k = 0; k < 150 && fget("opens") == opens; k++) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        CHECK(fget("ctl_timeouts") == 2 && fget("opens") == opens + 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(receive(ring, 150).n > 0);
        r.centerHz = 101e6;
        err.clear();
        CHECK(rx->retune(r, err));
        CHECK(fget("rx_freq") == 101e6);
        CHECK(statusWhen([](const OmniCoreStatus& x) { return x.rssiKnown; }).rssiKnown);
        rx->stop();
        CHECK(fget("open") == 0);
    }

    // ---- USB 2.0: a note above what it carries
    fset("usb2", 1);
    {
        TuneSettings r;
        r.centerHz = 100e6; r.sampleRate = 20e6;
        err.clear();
        CHECK(rx->start(r, ring, err));
        CHECK(err.find("USB 2.0") != std::string::npos);
        rx->stop();
    }
    fset("usb2", 0);

    // ---- restarts release the radio each time
    for (int k = 0; k < 3; k++) {
        err.clear();
        CHECK(rx->start(t, ring, err));
        CHECK(receive(ring, 60).n > 0);
        rx->stop();
        CHECK(fget("open") == 0);
    }

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
