// A fake OmniCore Host library (omnicore.dll / libomnicore.so, OCH ABI 1) for tests/source/test_omnicore.cpp. It keeps the rules of the real
// library that OnAir's driver (source_omnicore.cpp) depends on: one open per radio, the channel masks cannot change while a stream exists,
// a refused configuration stops both streams, an explicitly stopped stream cannot start again. The receive stream delivers a tone of
// amplitude 0.5 at +100 kHz plus a DC offset of 0.1 while DC correction is off, paced at the configured rate; the transmit stream takes
// samples at that rate (and, as OCH, starts only with two USB transfers queued). fake_get / fake_set read and set its record.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#if defined(_WIN32)
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
struct device_info {
    uint32_t struct_size, abi_version;
    char serial[128], name[128], driver[32], firmware[128];
    uint32_t usb_speed_mbps, rx_channels, tx_channels, hardware_timestamps, max_sample_rate;
};
struct config {
    uint32_t struct_size, sample_rate, rx_mask, tx_mask;
    uint64_t rx_frequency, tx_frequency;
    uint32_t rx_bandwidth, tx_bandwidth;
    double rx_gain[2], tx_attenuation[2];
    uint32_t rx_agc, usb_iq_bits, reserved[6];
};
struct stream_args { uint32_t struct_size, direction, format, transfer_bytes, usb_queue_depth, host_buffer_bytes, reserved[10]; };
struct metadata { uint32_t struct_size, flags; uint64_t epoch, sample_index, host_dropped_samples; int64_t device_dropped_samples; uint64_t host_monotonic_ns; };
struct event { uint32_t struct_size, type; uint64_t sequence, host_monotonic_ns, epoch; char message[512]; };
struct rx_correction { uint32_t struct_size, dc_enabled, iq_enabled, reserved[5]; };
struct device;
struct stream {
    device* dev;
    int dir;
    std::atomic<bool> started{false}, stopped{false};
    double phase = 0;
    std::chrono::steady_clock::time_point t0;
    uint64_t done = 0;
    uint32_t transferBytes = 196608;
    uint64_t queued = 0;   // TX samples sent before start
};
struct device {
    config cfg{};
    bool configured = false;
    stream* rx = nullptr;
    stream* tx = nullptr;
    uint32_t dc = 1, iq = 1;
};

std::mutex gMu;              // the record
std::mutex gDevMu;           // the device's state (a coarse lock, as OCH's state_mutex)
std::map<std::string, double> gState;
thread_local std::string gErr;
device* gOpen = nullptr;

void rec(const std::string& k, double v) { std::lock_guard<std::mutex> lk(gMu); gState[k] = v; }
double get(const std::string& k) { std::lock_guard<std::mutex> lk(gMu); auto it = gState.find(k); return it == gState.end() ? 0 : it->second; }
void bump(const std::string& k) { std::lock_guard<std::mutex> lk(gMu); gState[k] += 1; }
int fail(int code, const char* why) { gErr = why; return code; }
void record(const config& c) {
    rec("rate", c.sample_rate); rec("rx_mask", c.rx_mask); rec("tx_mask", c.tx_mask);
    rec("rx_freq", (double)c.rx_frequency); rec("tx_freq", (double)c.tx_frequency);
    rec("rx_bw", c.rx_bandwidth); rec("tx_bw", c.tx_bandwidth);
    rec("rx_gain", c.rx_gain[0]); rec("tx_atten", c.tx_attenuation[0]); rec("rx_agc", c.rx_agc);
}
// what the real library's clock plan does for 11.2 Msps
uint32_t applied(uint32_t r) { return r == 11200000 ? 11200011 : r; }
} // namespace

EXPORT double fake_get(const char* k) { return get(k); }
EXPORT void fake_set(const char* k, double v) { rec(k, v); }

EXPORT const char* och_version() { return "0.4.6-fake"; }
EXPORT const char* och_last_error() { return gErr.c_str(); }
EXPORT void och_default_config(config* c) {
    *c = {};
    c->struct_size = sizeof *c; c->sample_rate = 30720000; c->rx_mask = 3; c->tx_mask = 0;
    c->rx_frequency = c->tx_frequency = 1000000000ULL; c->rx_bandwidth = c->tx_bandwidth = 18000000;
    c->rx_gain[0] = c->rx_gain[1] = 30; c->tx_attenuation[0] = c->tx_attenuation[1] = 10; c->rx_agc = 2;
}
EXPORT void och_default_stream_args(stream_args* a, int dir) {
    *a = {}; a->struct_size = sizeof *a; a->direction = (uint32_t)dir; a->format = 1; a->transfer_bytes = 196608; a->usb_queue_depth = 32; a->host_buffer_bytes = 64u << 20;
}
EXPORT int och_list(device_info* out, size_t cap, size_t* count) {
    if (!count || (cap && !out)) return fail(-2, "invalid device list output");
    const bool present = get("gone") == 0;
    *count = present ? 1 : 0;
    if (present && cap) {
        if (out[0].struct_size != sizeof(device_info)) return fail(-2, "output struct_size mismatch");
        out[0] = {};
        out[0].struct_size = sizeof(device_info); out[0].abi_version = 1;
        strcpy(out[0].serial, "FAKE0001"); strcpy(out[0].name, "OmniCoreSDR"); strcpy(out[0].driver, "libusbK");
        out[0].rx_channels = out[0].tx_channels = 2; out[0].max_sample_rate = 61440000;
    }
    return 0;
}
EXPORT int och_open(const char* serial, device** out) {
    std::lock_guard<std::mutex> lk(gDevMu);
    if (get("gone") != 0) return fail(-3, "OmniCoreSDR with libusbK not found");
    if (serial && *serial && strcmp(serial, "FAKE0001") != 0) return fail(-3, "no such serial");
    if (gOpen) return fail(-4, "device already open");
    gOpen = new device;
    bump("opens");
    rec("open", 1);
    *out = gOpen;
    return 0;
}
EXPORT int och_close(device* d) {
    std::lock_guard<std::mutex> lk(gDevMu);
    if (d->rx || d->tx) return fail(-4, "destroy RX/TX streams before closing device");
    delete d;
    gOpen = nullptr;
    rec("open", 0);
    return 0;
}
EXPORT int och_get_info(device*, device_info* i) {
    if (i->struct_size != sizeof(device_info)) return fail(-2, "output struct_size mismatch");
    *i = {};
    i->struct_size = sizeof *i; i->abi_version = 1;
    strcpy(i->serial, "FAKE0001"); strcpy(i->name, "OmniCoreSDR"); strcpy(i->firmware, "fake");
    i->usb_speed_mbps = (uint32_t)(get("usb2") != 0 ? 480 : 5000);
    i->rx_channels = i->tx_channels = 2; i->max_sample_rate = 61440000;
    return 0;
}
EXPORT int och_configure(device* d, const config* c) {
    std::lock_guard<std::mutex> lk(gDevMu);
    if (c->struct_size != sizeof(config)) return fail(-2, "och_config ABI size mismatch");
    if ((d->rx || d->tx) && d->configured && (c->rx_mask != d->cfg.rx_mask || c->tx_mask != d->cfg.tx_mask))
        return fail(-2, "channel masks cannot change while streams exist");
    if (!c->sample_rate || c->sample_rate > 61440000 || (!c->rx_mask && !c->tx_mask) || c->rx_mask > 3 || c->tx_mask > 3) return fail(-2, "invalid rate/channel masks");
    const bool badFreq = c->rx_frequency < 70000000 || c->rx_frequency > 6000000000ULL || c->tx_frequency < 70000000 || c->tx_frequency > 6000000000ULL;
    const bool badBw = c->rx_bandwidth < 200000 || c->rx_bandwidth > 56000000 || c->tx_bandwidth < 200000 || c->tx_bandwidth > 56000000;
    const double maxGain = c->rx_frequency <= 1300000000ULL ? 73 : c->rx_frequency <= 4000000000ULL ? 71 : 62;
    const bool badGain = c->rx_gain[0] < -3 || c->rx_gain[0] > maxGain || c->rx_gain[0] != std::floor(c->rx_gain[0]) ||
                         c->tx_attenuation[0] < 0 || c->tx_attenuation[0] > 89.75 || c->tx_attenuation[0] * 4 != std::floor(c->tx_attenuation[0] * 4);
    if (badFreq || badBw || badGain) {
        bump("refused");
        // a refused hardware transaction stops both directions (OCH's reconfiguration failure)
        if (d->rx) d->rx->stopped = true;
        if (d->tx) d->tx->stopped = true;
        d->configured = false;
        return fail(badFreq ? -2 : -9, badFreq ? "RF frequency must be 70 MHz..6 GHz" : badBw ? "RF bandwidth must be 200 kHz..56 MHz" : "gain out of range for this band");
    }
    config a = *c;
    a.sample_rate = applied(c->sample_rate);
    const bool tuningOnly = d->configured && a.sample_rate == d->cfg.sample_rate && a.rx_bandwidth == d->cfg.rx_bandwidth &&
                            a.tx_bandwidth == d->cfg.tx_bandwidth && a.rx_agc == d->cfg.rx_agc;
    if (!tuningOnly && (d->rx || d->tx)) bump("pauses");   // OCH pauses and resumes the streams for this change
    d->cfg = a;
    d->configured = true;
    bump("configures");
    record(a);
    return 0;
}
EXPORT int och_get_config(device* d, config* c) {
    std::lock_guard<std::mutex> lk(gDevMu);
    if (c->struct_size != sizeof(config)) return fail(-2, "output struct_size mismatch");
    if (!d->configured) return fail(-2, "device has not been configured by this handle");
    *c = d->cfg;
    return 0;
}
EXPORT int och_poll_event(device*, event* e, uint32_t) {
    if (e->struct_size != sizeof(event)) return fail(-2, "output struct_size mismatch");
    return -1;
}
EXPORT int och_create_stream(device* d, const stream_args* a, stream** out) {
    std::lock_guard<std::mutex> lk(gDevMu);
    if (a->struct_size != sizeof(stream_args) || a->format != 2) return fail(-2, "fake: FC32 only");
    if (!d->configured) return fail(-2, "configure first");
    if (a->direction == 0 ? (!d->cfg.rx_mask || d->rx) : (!d->cfg.tx_mask || d->tx)) return fail(-2, "direction not configured or stream exists");
    auto* s = new stream;
    s->dev = d; s->dir = (int)a->direction; s->transferBytes = a->transfer_bytes;
    (a->direction == 0 ? d->rx : d->tx) = s;
    bump(a->direction == 0 ? "rx_streams" : "tx_streams");
    *out = s;
    return 0;
}
EXPORT int och_destroy_stream(stream* s) {
    std::lock_guard<std::mutex> lk(gDevMu);
    (s->dir == 0 ? s->dev->rx : s->dev->tx) = nullptr;
    delete s;
    return 0;
}
EXPORT int och_start(stream* s) {
    if (s->stopped) return fail(-7, "stopped streams cannot be restarted");
    // as OCH: a transmit stream starts only with two USB transfers of wire data queued (12-bit: 3 bytes a sample, one channel here)
    if (s->dir == 1 && s->queued * 3 < 2ull * s->transferBytes) return fail(-4, "queue at least two transfer blocks of TX samples before start");
    s->t0 = std::chrono::steady_clock::now();
    s->started = true;
    return 0;
}
EXPORT int och_stop(stream* s, uint32_t) { s->stopped = true; return 0; }
EXPORT int och_flush(stream* s, uint32_t) { bump("flushes"); return s->dir == 1 ? 0 : -2; }
EXPORT int och_recv(stream* s, void* out, size_t cap, size_t* got, metadata* m, uint32_t timeout) {
    *got = 0;
    if (m->struct_size != sizeof(metadata)) return fail(-2, "output struct_size mismatch");
    if (s->stopped) return fail(-7, "stream stopped");
    if (!s->started) { std::this_thread::sleep_for(std::chrono::milliseconds(std::min<uint32_t>(timeout, 5))); return -1; }
    double rate, dcOff;
    {
        std::lock_guard<std::mutex> lk(gDevMu);
        rate = s->dev->cfg.sample_rate;
        dcOff = s->dev->dc ? 0 : 0.1;
    }
    // paced at the rate: what is due since the start
    const size_t n = std::min<size_t>(cap, 4096);
    const auto due = s->t0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>((s->done + n) / rate));
    if (due > std::chrono::steady_clock::now()) std::this_thread::sleep_until(std::min(due, std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout)));
    if (due > std::chrono::steady_clock::now()) return -1;
    float* f = static_cast<float*>(out);
    const double step = 2 * M_PI * 100e3 / rate;
    for (size_t k = 0; k < n; k++) {
        f[2 * k] = (float)(0.5 * std::cos(s->phase) + dcOff);
        f[2 * k + 1] = (float)(0.5 * std::sin(s->phase) + dcOff);
        s->phase = std::fmod(s->phase + step, 2 * M_PI);
    }
    s->done += n;
    *got = n;
    return 0;
}
EXPORT int och_send(stream* s, const void* data, size_t count, size_t* accepted, uint32_t timeout) {
    *accepted = 0;
    if (s->stopped) return fail(-7, "stream stopped");
    if (!s->started) { const size_t n = std::min<size_t>(count, 65536); s->queued += n; *accepted = n; return 0; }   // queued before start, in parts
    double rate;
    { std::lock_guard<std::mutex> lk(gDevMu); rate = s->dev->cfg.sample_rate; }
    const size_t n = std::min<size_t>(count, 8192);
    const auto due = s->t0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>((s->done + n) / rate));
    if (due > std::chrono::steady_clock::now()) std::this_thread::sleep_until(std::min(due, std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout)));
    if (due > std::chrono::steady_clock::now()) return -1;
    const float* f = static_cast<const float*>(data);
    double p = 0;
    for (size_t k = 0; k < n; k++) p = std::max(p, (double)std::fabs(f[2 * k]));
    rec("tx_peak", p);
    s->done += n;
    { std::lock_guard<std::mutex> lk(gMu); gState["tx_samples"] += (double)n; }
    *accepted = n;
    return 0;
}
EXPORT int och_get_rx_correction(device* d, rx_correction* c) {
    if (c->struct_size != sizeof(rx_correction)) return fail(-2, "output struct_size mismatch");
    std::lock_guard<std::mutex> lk(gDevMu);
    c->dc_enabled = d->dc; c->iq_enabled = d->iq;
    return 0;
}
EXPORT int och_set_rx_dc_correction(device* d, uint32_t on) {
    if (get("nocorr") != 0) return fail(-6, "SDK lacks rx_correction");
    std::lock_guard<std::mutex> lk(gDevMu);
    d->dc = on; rec("dc", on);
    return 0;
}
EXPORT int och_set_rx_iq_correction(device* d, uint32_t on) {
    if (get("nocorr") != 0) return fail(-6, "SDK lacks rx_correction");
    std::lock_guard<std::mutex> lk(gDevMu);
    d->iq = on; rec("iq", on);
    return 0;
}
