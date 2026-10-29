// Wenet LDPC code: encoder and decoder, see wenet.h.
// The code is the rate 0.8 repeat-accumulate code of the Wenet link (tables in wenet_tables.cpp): parity bit p is the XOR of 12 information bits
// (kHrows) and the previous parity bit. As a parity-check matrix every check p holds those 12 bits, parity bit p and parity bit p - 1.
// The decoder is a layered normalised min-sum decoder written for OnAir (the Wenet reference decoder is a sum-product decoder from codec2).
#include "dect2/ssdv/wenet.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace dect2 {
namespace wenet {

uint16_t crc16(const uint8_t* d, size_t n) {
    // CRC-16/CCITT-FALSE, init 0xFFFF (the same as Python's binascii.crc_hqx(data, 0xffff) that the Wenet transmitter uses)
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)((uint16_t)d[i] << 8);
        for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

void ldpcEncode(const uint8_t* info, uint8_t* parity) {
    uint8_t bit[kInfoBits];
    for (int i = 0; i < kInfoBits; i++) bit[i] = (uint8_t)((info[i >> 3] >> (7 - (i & 7))) & 1);
    std::memset(parity, 0, kParityBytes);
    unsigned prev = 0;
    for (int p = 0; p < kParityBits; p++) {
        unsigned s = prev;
        for (int i = 0; i < 12; i++) s += bit[kHrows[p * 12 + i] - 1];
        prev = s & 1;
        if (prev) parity[p >> 3] = (uint8_t)(parity[p >> 3] | (0x80 >> (p & 7)));
    }
}

namespace {

struct Graph {
    int var[kParityBits][14];
    int deg[kParityBits];
    Graph() {
        for (int c = 0; c < kParityBits; c++) {
            int k = 0;
            for (int i = 0; i < 12; i++) var[c][k++] = kHrows[c * 12 + i] - 1;
            var[c][k++] = kInfoBits + c;
            if (c > 0) var[c][k++] = kInfoBits + c - 1;
            deg[c] = k;
        }
    }
};

const Graph& graph() { static const Graph g; return g; }

bool syndromeOk(const Graph& g, const uint8_t* hard) {
    for (int c = 0; c < kParityBits; c++) {
        unsigned s = 0;
        for (int k = 0; k < g.deg[c]; k++) s ^= hard[g.var[c][k]];
        if (s) return false;
    }
    return true;
}

} // namespace

bool ldpcDecode(const float* soft, uint8_t* bits, int maxIter, int* iterations) {
    const Graph& g = graph();
    static thread_local float total[kCodeBits];
    static thread_local float c2v[kParityBits][14];
    for (int i = 0; i < kCodeBits; i++) { total[i] = soft[i]; bits[i] = soft[i] > 0 ? 1 : 0; }
    if (iterations) *iterations = 0;
    if (syndromeOk(g, bits)) return true;
    std::memset(c2v, 0, sizeof c2v);
    const float alpha = 0.8f;
    for (int it = 1; it <= maxIter; it++) {
        for (int c = 0; c < kParityBits; c++) {
            const int d = g.deg[c];
            float v2c[14];
            float m1 = 1e30f, m2 = 1e30f;
            int i1 = -1;
            unsigned sg = 0;
            for (int k = 0; k < d; k++) {
                const float v = total[g.var[c][k]] - c2v[c][k];
                v2c[k] = v;
                const float a = std::fabs(v);
                if (v < 0) sg ^= 1;
                if (a < m1) { m2 = m1; m1 = a; i1 = k; } else if (a < m2) m2 = a;
            }
            for (int k = 0; k < d; k++) {
                const float mag = alpha * (k == i1 ? m2 : m1);
                const unsigned sk = sg ^ (v2c[k] < 0 ? 1u : 0u);
                const float nv = sk ? -mag : mag;
                c2v[c][k] = nv;
                total[g.var[c][k]] = v2c[k] + nv;
            }
        }
        for (int i = 0; i < kCodeBits; i++) bits[i] = total[i] > 0 ? 1 : 0;
        if (syndromeOk(g, bits)) { if (iterations) *iterations = it; return true; }
    }
    if (iterations) *iterations = maxIter;
    return false;
}

std::vector<uint8_t> frameBits(const uint8_t* payload, Framing f, int preambleBytes) {
    uint8_t info[kInfoBytes], body[kBodyBytes];
    std::memcpy(info, payload, kPayloadBytes);
    const uint16_t crc = crc16(payload, kPayloadBytes);
    info[256] = (uint8_t)(crc & 0xFF);
    info[257] = (uint8_t)(crc >> 8);
    std::memcpy(body, info, kInfoBytes);
    ldpcEncode(info, body + kInfoBytes);
    if (f == Framing::Raw) for (int i = 0; i < kBodyBytes; i++) body[i] = (uint8_t)(body[i] ^ kScramble[i % 125]);
    std::vector<uint8_t> out;
    out.reserve((size_t)(preambleBytes + 4 + kBodyBytes) * 10);
    auto push = [&](uint8_t v) {
        if (f == Framing::Uart) { out.push_back(0); for (int i = 0; i < 8; i++) out.push_back((uint8_t)((v >> i) & 1)); out.push_back(1); }
        else for (int i = 7; i >= 0; i--) out.push_back((uint8_t)((v >> i) & 1));
    };
    for (int i = 0; i < preambleBytes; i++) push(0x55);
    push(0xAB); push(0xCD); push(0xEF); push(0x01);
    for (int i = 0; i < kBodyBytes; i++) push(body[i]);
    return out;
}

} // namespace wenet
} // namespace dect2
