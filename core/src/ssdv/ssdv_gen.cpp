// SSDV test signal, see ssdv_gen.h
#include "dect2/ssdv/ssdv_gen.h"
#include "dect2/ssdv/ssdv_rx.h"
#include "dect2/ssdv/wenet.h"
#include "dect2/common/mathconst.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <random>

namespace dect2 {

struct SsdvGenerator::Impl {
    SsdvGenConfig c;
    std::mt19937 rng;
    std::normal_distribution<double> nd{0.0, 1.0};
    std::deque<uint8_t> bits;
    size_t nextPacket = 0;
    long slipCount = 0;
    double phase = 0, symPhase = 0, curBit = 1, smooth = 0, t = 0;
    double sigma = 0;

    explicit Impl(const SsdvGenConfig& cfg) : c(cfg), rng(cfg.seed) {
        if (c.wenet) {
            if (c.baud == 1200) c.baud = c.wenet == 2 ? wenet::kRawBaud : wenet::kUartBaud;
            if (c.deviationHz <= 0) c.deviationHz = c.baud * 0.6233;   // 71.8 kHz at 115.2 kbaud
        }
        if (c.deviationHz <= 0) c.deviationHz = c.async ? std::max(500.0, c.baud * 1.5) : std::max(1200.0, c.baud * 0.5);
        if (c.ebn0Db < 99) {
            const double n0 = c.amp * c.amp / (c.baud * std::pow(10.0, c.ebn0Db / 10.0));
            sigma = std::sqrt(n0 * c.rate / 2);   // per component
        }
    }
    void pushByte(uint8_t v) {
        if (!c.async) { for (int i = 7; i >= 0; i--) bits.push_back((v >> i) & 1); return; }
        bits.push_back(0);
        for (int i = 0; i < 8; i++) bits.push_back((v >> i) & 1);
        bits.push_back(1); bits.push_back(1);
    }
    void refill() {
        if (c.packets.empty()) { pushByte(0x55); return; }
        if (c.wenet) {
            const auto fb = wenet::frameBits(c.packets[nextPacket].data(), c.wenet == 2 ? wenet::Framing::Raw : wenet::Framing::Uart, c.preambleBytes);
            for (uint8_t b : fb) bits.push_back(b);
            nextPacket = (nextPacket + 1) % c.packets.size();
            return;
        }
        if (nextPacket == 0) for (int i = 0; i < c.preambleBytes; i++) pushByte(0x55);
        for (uint8_t v : c.packets[nextPacket]) pushByte(v);
        nextPacket = (nextPacket + 1) % c.packets.size();
        for (int i = 0; i < (int)c.gapBytes; i++) pushByte(0x55);
        if (c.async) for (int i = 0; i < 4; i++) bits.push_back(1);
    }
    void generate(cf32* out, size_t n) {
        const double baud = c.baud * (1 + c.clockPpm * 1e-6);
        const double sps = c.rate / baud;
        // Gaussian filter, approximated by a one-pole smoother whose time constant follows BT
        const double alpha = c.gaussBt > 0 ? 1.0 - std::exp(-1.0 / (sps * 0.35 / c.gaussBt)) : 1.0;
        for (size_t i = 0; i < n; i++) {
            symPhase += 1.0;
            if (symPhase >= sps) {
                symPhase -= sps;
                if (bits.empty()) refill();
                curBit = bits.front() ? 1.0 : -1.0;
                bits.pop_front();
                if (c.slipEveryBits != 0 && ++slipCount >= std::abs(c.slipEveryBits)) {
                    slipCount = 0;
                    if (c.slipEveryBits > 0) { if (bits.empty()) refill(); bits.pop_front(); }   // a bit is lost
                    else bits.push_front(bits.empty() ? 1 : bits.front());                         // a bit is repeated
                }
                if (c.invert) curBit = -curBit;
            }
            smooth += (curBit - smooth) * alpha;
            const double f = c.dialOffsetHz + c.cfoHz + c.driftHzPerSec * t + c.deviationHz * smooth;
            phase += 2 * math::kPi * f / c.rate;
            if (phase > math::kPi) phase -= 2 * math::kPi; else if (phase < -math::kPi) phase += 2 * math::kPi;
            double re = c.amp * std::cos(phase), im = c.amp * std::sin(phase);
            if (sigma > 0) { re += sigma * nd(rng); im += sigma * nd(rng); }
            out[i] = cf32((float)re, (float)im);
            t += 1.0 / c.rate;
        }
    }
};

SsdvGenerator::SsdvGenerator(const SsdvGenConfig& c) : p_(std::make_unique<Impl>(c)) {}
SsdvGenerator::~SsdvGenerator() = default;
void SsdvGenerator::generate(cf32* out, size_t n) { p_->generate(out, n); }

namespace {
class SsdvSynth : public ModeSynth {
public:
    SsdvSynth(const SynthConfig& s, double rate) : rate_(rate) {
        SsdvGenConfig c;
        c.rate = rate;
        c.dialOffsetHz = -ssdvTuning().tuneOffsetHz;
        c.cfoHz = s.cfoHz;
        c.ebn0Db = s.snrDb;
        c.async = s.modeOpt[0] == 1;
        c.baud = s.modeVal[0] > 0 ? s.modeVal[0] : 1200;
        c.gaussBt = c.async ? 0 : 0.5;
        c.packets = ssdv::encodePackets(ssdv::testJpeg(96, 64), "OnAir", 7, 5, false);
        gen_ = std::make_unique<SsdvGenerator>(c);
    }
    double sampleRate() const override { return rate_; }
    void generate(cf32* out, size_t n) override { gen_->generate(out, n); }
private:
    double rate_;
    std::unique_ptr<SsdvGenerator> gen_;
};
} // namespace

std::unique_ptr<ModeSynth> makeSsdvSynth(const SynthConfig& cfg, double sampleRate) { return std::make_unique<SsdvSynth>(cfg, sampleRate); }

} // namespace dect2
