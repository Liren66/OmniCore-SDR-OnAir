// Wenet demodulator and frame sync, see wenet.h.
// IQ -> mix out the offset and the AFC -> low-pass (+-120 kHz) and decimate to 700 kHz .. 1.3 MHz -> FM discriminator -> two bit slicers (115.2 kbaud
// serial-word framing, 96 kbaud raw scrambled) with a box integrator of 0.8 bit and a transition-driven clock loop -> unique word search (both
// polarities) -> soft symbols of the frame -> LDPC -> CRC-16.
#include "dect2/ssdv/wenet.h"
#include "dect2/common/mathconst.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace dect2 {
namespace wenet {

namespace {

constexpr int kUartUwBits = 40, kRawUwBits = 32;
constexpr int kUartBodySyms = kBodyBytes * 10, kRawBodySyms = kBodyBytes * 8;

int pop64(uint64_t v) { int n = 0; while (v) { v &= v - 1; n++; } return n; }

struct Cap {
    bool inv = false;
    bool exact = false;
    int n = 0;
    std::vector<float> s;
};

struct Chain {
    int baud = kUartBaud;
    bool uart = true;
    int uwBits = kUartUwBits, uwTol = 5, bodySyms = kUartBodySyms;
    uint64_t uwVal = 0, uwMask = 0;
    // sampler
    double rs = 1e6;
    float step0 = 0.1f, step = 0.1f;
    std::vector<float> box;
    size_t bpos = 0;
    double bsum = 0;
    float ph = -0.5f;
    bool lev = false, prevLev = false;
    uint64_t sr = 0;
    int holdoff = 0;
    std::vector<Cap> caps;

    void setup(int bd, bool isUart, double rate) {
        baud = bd; uart = isUart; rs = rate;
        uwBits = uart ? kUartUwBits : kRawUwBits;
        uwTol = uart ? 5 : 4;
        bodySyms = uart ? kUartBodySyms : kRawBodySyms;
        uwMask = uwBits == 64 ? ~0ull : ((1ull << uwBits) - 1);
        const uint8_t uw[4] = {0xAB, 0xCD, 0xEF, 0x01};
        uint64_t v = 0;
        for (uint8_t b : uw) {
            if (uart) { v = (v << 1) | 0; for (int i = 0; i < 8; i++) v = (v << 1) | ((b >> i) & 1u); v = (v << 1) | 1; }
            else for (int i = 7; i >= 0; i--) v = (v << 1) | ((b >> i) & 1u);
        }
        uwVal = v;
        step0 = step = (float)(baud / rate);
        const double sps = rate / baud;
        box.assign((size_t)std::max(2.0, std::round(sps * 0.8)), 0.f);
        reset();
    }
    void reset() {
        std::fill(box.begin(), box.end(), 0.f);
        bpos = 0; bsum = 0; ph = -0.5f; lev = prevLev = false; sr = 0; holdoff = 0; caps.clear(); step = step0;
    }
};

} // namespace

struct Demod::Impl {
    double fs = 0, rs = 0, offsetHz = 0;
    int dec = 1;
    bool isReady = false;
    int mask = kMaskUart | kMaskRaw;
    std::function<void(const Hit&)> cb;
    uint64_t good = 0, bad = 0;
    // front end
    std::vector<float> taps;                 // symmetric low-pass, odd length
    std::vector<cf32> hist;                  // 2 * N, written twice so a window is contiguous
    size_t hpos = 0;
    int decCount = 0;
    double nco = 0, ncoRe = 1, ncoIm = 0, rotRe = 1, rotIm = 0;
    double afc = 0;
    int updCount = 0;
    cf32 prevY = cf32(1.f, 0.f);
    Chain uart, raw;
    std::vector<float> cw;
    uint8_t bits[kCodeBits];

    bool configure(double rate) {
        isReady = false;
        fs = rate;
        if (rate < 700e3) return false;
        dec = std::max(1, (int)std::floor(rate / 920e3));
        rs = rate / dec;
        const double cutoff = 120e3, trans = 150e3;   // 120 kHz: about 1.5 dB better at the threshold than 160 kHz, still wide enough for the +-72 kHz tones
        int n = (int)std::ceil(3.3 * rate / trans);
        if (n % 2 == 0) n++;
        taps.assign((size_t)n, 0.f);
        const double fc = cutoff / rate, m = (n - 1) / 2.0;
        double sum = 0;
        for (int i = 0; i < n; i++) {
            const double x = i - m;
            const double sinc = x == 0 ? 2 * fc : std::sin(2 * math::kPi * fc * x) / (math::kPi * x);
            const double w = 0.54 - 0.46 * std::cos(2 * math::kPi * i / (n - 1));
            taps[(size_t)i] = (float)(sinc * w);
            sum += sinc * w;
        }
        for (auto& t : taps) t = (float)(t / sum);
        hist.assign((size_t)n * 2, cf32(0.f, 0.f));
        uart.setup(kUartBaud, true, rs);
        raw.setup(kRawBaud, false, rs);
        cw.assign((size_t)kBodyBytes * 8, 0.f);   // the UART body is 323 whole bytes: 4 bits more than the code word
        isReady = true;
        resetState();
        return true;
    }

    void setRot() {
        const double w = -2 * math::kPi * (offsetHz + afc) / fs;
        rotRe = std::cos(w); rotIm = std::sin(w);
    }

    void resetState() {
        std::fill(hist.begin(), hist.end(), cf32(0.f, 0.f));
        hpos = 0; decCount = 0; nco = 0; ncoRe = 1; ncoIm = 0; afc = 0; updCount = 0; prevY = cf32(1.f, 0.f);
        uart.reset(); raw.reset();
        setRot();
    }

    void emit(const Chain& c, const uint8_t* pkt, bool inv, int iters, float snr) {
        good++;
        if (!cb) return;
        Hit h;
        std::memcpy(h.pkt.data(), pkt, kPayloadBytes);
        h.baud = c.baud; h.uart = c.uart; h.inverted = inv; h.iterations = iters; h.snrDb = snr; h.cfoHz = (float)afc;
        cb(h);
    }

    void decodeCap(Chain& c, Cap& cap) {
        const float* s = cap.s.data();
        if (c.uart) {
            for (int k = 0; k < kBodyBytes; k++)
                for (int i = 0; i < 8; i++) cw[(size_t)(8 * k + 7 - i)] = s[10 * k + 1 + i];
        } else {
            for (int j = 0; j < kCodeBits; j++) {
                const int sb = (kScramble[(j >> 3) % 125] >> (7 - (j & 7))) & 1;
                cw[(size_t)j] = sb ? -s[j] : s[j];
            }
        }
        double mean = 0;
        for (int j = 0; j < kCodeBits; j++) mean += std::fabs(cw[(size_t)j]);
        mean /= kCodeBits;
        if (mean <= 0) return;
        // decision SNR of the frame and a clamp against outliers
        double sum = 0, sumsq = 0;
        for (int j = 0; j < kCodeBits; j++) {
            float& v = cw[(size_t)j];
            const double x = v / mean - (v > 0 ? 1.0 : -1.0);
            sum += x; sumsq += x * x;
            v = (float)std::max(-4.0, std::min(4.0, v / mean));
        }
        const double var = (kCodeBits * sumsq - sum * sum) / ((double)kCodeBits * (kCodeBits - 1));
        const float snr = var > 1e-6 ? (float)(10 * std::log10(1.0 / var)) : 40.f;
        int iters = 0;
        ldpcDecode(cw.data(), bits, 40, &iters);
        uint8_t info[kInfoBytes];
        for (int k = 0; k < kInfoBytes; k++) {
            uint8_t b = 0;
            for (int i = 0; i < 8; i++) b = (uint8_t)((b << 1) | bits[8 * k + i]);
            info[k] = b;
        }
        const uint16_t want = (uint16_t)(info[256] | (info[257] << 8));
        if (crc16(info, kPayloadBytes) == want) emit(c, info, cap.inv, iters, snr);
        else if (cap.exact) bad++;
    }

    void onBit(Chain& c, float soft) {
        for (size_t i = 0; i < c.caps.size();) {
            Cap& cap = c.caps[i];
            cap.s[(size_t)cap.n++] = cap.inv ? -soft : soft;
            if (cap.n >= c.bodySyms) { Cap done = std::move(cap); c.caps.erase(c.caps.begin() + (long)i); decodeCap(c, done); continue; }
            i++;
        }
        c.sr = (c.sr << 1) | (soft > 0 ? 1u : 0u);
        if (c.holdoff > 0) { c.holdoff--; return; }
        const int e = pop64((c.sr ^ c.uwVal) & c.uwMask);
        int pol = 0;
        if (e <= c.uwTol) pol = 1; else if (c.uwBits - e <= c.uwTol) pol = 2;
        if (!pol) return;
        if (c.caps.size() >= 3) c.caps.erase(c.caps.begin());
        Cap cap;
        cap.inv = pol == 2;
        cap.exact = e == 0 || e == c.uwBits;
        cap.s.assign((size_t)c.bodySyms, 0.f);
        c.caps.push_back(std::move(cap));
        c.holdoff = c.uwBits / 2;
    }

    void runChain(Chain& c, float x) {
        c.bsum += x - c.box[c.bpos];
        c.box[c.bpos] = x;
        if (++c.bpos == c.box.size()) c.bpos = 0;
        const float v = (float)(c.bsum / (double)c.box.size());
        c.lev = v > 0;
        if (c.lev != c.prevLev) {
            // a transition of the box output sits at phase 0 (the sampling instant is half a bit later)
            const float e = c.ph;
            c.ph -= 0.2f * e;
            c.step -= 0.004f * c.step0 * e;
            c.step = std::max(c.step0 * 0.99f, std::min(c.step0 * 1.01f, c.step));
        }
        c.prevLev = c.lev;
        c.ph += c.step;
        if (c.ph >= 0.5f) { c.ph -= 1.f; onBit(c, v); }
    }

    void onDisc(float hz) {
        // AFC: the mean of the discriminator is the carrier error (the data is balanced)
        const double cl = std::max(-150e3, std::min(150e3, (double)hz));
        afc += 4e-4 * cl;
        afc -= afc * 1e-5;
        afc = std::max(-110e3, std::min(110e3, afc));
        if (++updCount >= 32) { updCount = 0; setRot(); }
        const float x = hz * (1.f / 1e5f);
        if ((mask & kMaskUart)) runChain(uart, x);
        if ((mask & kMaskRaw)) runChain(raw, x);
    }

    void feed(const cf32* x, size_t n) {
        if (!isReady) return;
        const size_t N = taps.size(), half = N / 2;
        const float* t = taps.data();
        for (size_t i = 0; i < n; i++) {
            // mix
            const double mr = x[i].real() * ncoRe - x[i].imag() * ncoIm;
            const double mi = x[i].real() * ncoIm + x[i].imag() * ncoRe;
            const double nr = ncoRe * rotRe - ncoIm * rotIm;
            const double ni = ncoRe * rotIm + ncoIm * rotRe;
            ncoRe = nr; ncoIm = ni;
            if ((i & 1023) == 1023) { const double inv = 1.0 / std::sqrt(ncoRe * ncoRe + ncoIm * ncoIm); ncoRe *= inv; ncoIm *= inv; }
            hist[hpos] = hist[hpos + N] = cf32((float)mr, (float)mi);
            if (++hpos == N) hpos = 0;
            if (++decCount < dec) continue;
            decCount = 0;
            // window: hist[hpos .. hpos + N - 1], oldest first
            const cf32* w = &hist[hpos];
            float ar = w[half].real() * t[half], ai = w[half].imag() * t[half];
            for (size_t k = 0; k < half; k++) {
                ar += (w[k].real() + w[N - 1 - k].real()) * t[k];
                ai += (w[k].imag() + w[N - 1 - k].imag()) * t[k];
            }
            const cf32 y(ar, ai);
            const cf32 d = y * std::conj(prevY);
            prevY = y;
            const float ang = std::atan2(d.imag(), d.real());
            onDisc((float)(ang * rs / (2 * math::kPi)));
        }
    }
};

Demod::Demod() : p_(std::make_unique<Impl>()) {}
Demod::~Demod() = default;
bool Demod::configure(double r) { return p_->configure(r); }
bool Demod::ready() const { return p_->isReady; }
void Demod::setOffset(double hz) { p_->offsetHz = hz; if (p_->isReady) p_->setRot(); }
void Demod::setMask(int m) { if (p_->mask != m) { p_->mask = m; if (p_->isReady) { p_->uart.reset(); p_->raw.reset(); } } }
void Demod::reset() { if (p_->isReady) p_->resetState(); }
void Demod::feed(const cf32* x, size_t n) { p_->feed(x, n); }
void Demod::setCallback(std::function<void(const Hit&)> cb) { p_->cb = std::move(cb); }
uint64_t Demod::goodFrames() const { return p_->good; }
uint64_t Demod::badFrames() const { return p_->bad; }
double Demod::afcHz() const { return p_->afc; }

} // namespace wenet
} // namespace dect2
