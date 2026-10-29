// LRRP over IP: the parser against the real packets of the 2026-10-23 capture (161.600 MHz, TG 64250).
#include "dect2/dmr/dmr_lrrp.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <vector>
using namespace dect2;
static int fails = 0;
static void fail(const char* fmt, ...) {   // one printf with a constant format, as Codacy wants
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    std::printf("FAIL: %s\n", b);
    fails++;
}
#define CHECK(c, ...) do { if (!(c)) fail(__VA_ARGS__); } while (0)

static std::vector<uint8_t> hex(const char* s) {
    std::vector<uint8_t> v;
    for (const char* p = s; p[0] && p[1]; p += 2) {
        unsigned u = 0;
        std::sscanf(p, "%2x", &u);
        v.push_back((uint8_t)u);
    }
    return v;
}

static bool near(double a, double b, double eps) { return std::fabs(a - b) < eps; }

int main() {
    // a fix: radio 183, a stationary unit. Matches DSDPlus.LRRP exactly: 53.35981 83.67784 2.025 km/h 142 deg.
    {
        std::vector<uint8_t> d = hex("45000033DE660000401187A20C0000B70D00FAFA0FA10FA1001FEB370D152203000001514BE3B6DC3B811656602D6C00485647");
        DmrLocation loc;
        CHECK(parseDmrLrrp(d.data(), d.size(), loc), "183: not parsed");
        CHECK(loc.hasFix, "183: no fix");
        CHECK(near(loc.lat, 53.35981, 1e-4), "183: lat %.6f", loc.lat);
        CHECK(near(loc.lon, 83.67785, 1e-4), "183: lon %.6f", loc.lon);
        CHECK(loc.hasSpeed && near(loc.speedMs, 0.5625, 1e-3), "183: speed %.4f m/s", loc.speedMs);
        CHECK(loc.hasHeading && near(loc.headingDeg, 142.0, 0.5), "183: heading %.1f", loc.headingDeg);
    }
    // the IPv4 header checksum of the real packets holds, and fails after one wrong bit (#335: a packet whose DMR CRC failed is kept on this check)
    {
        std::vector<uint8_t> d = hex("45000033DE660000401187A20C0000B70D00FAFA0FA10FA1001FEB370D152203000001514BE3B6DC3B811656602D6C00485647");
        CHECK(dmrIpv4HeaderOk(d.data(), d.size()), "ipv4 checksum of a real packet");
        d[15] ^= 0x04;
        CHECK(!dmrIpv4HeaderOk(d.data(), d.size()), "ipv4 checksum of a damaged packet");
    }
    // a moving radio: 115 at 22 km/h (6.13 m/s)
    {
        std::vector<uint8_t> d = hex("45000033441C0000401122310C0000730D00FAFA0FA10FA1001F376F0D152203000001514BE79AC13B829044025C6C06115632");
        DmrLocation loc;
        CHECK(parseDmrLrrp(d.data(), d.size(), loc), "115: not parsed");
        CHECK(loc.hasFix, "115: no fix");
        CHECK(near(loc.lat, 53.37050, 1e-4), "115: lat %.6f", loc.lat);
        CHECK(near(loc.lon, 83.68596, 1e-4), "115: lon %.6f", loc.lon);
        CHECK(loc.hasSpeed && near(loc.speedMs, 6.1328, 1e-3), "115: speed %.4f m/s", loc.speedMs);
        CHECK(loc.hasHeading && near(loc.headingDeg, 100.0, 0.5), "115: heading %.1f", loc.headingDeg);
    }
    // NoFix: no position, result code 16
    {
        std::vector<uint8_t> d = hex("450000251EBD00004011477A0C0000970D00FAFA0FA10FA100118BB70D0722030000013710");
        DmrLocation loc;
        CHECK(parseDmrLrrp(d.data(), d.size(), loc), "151: not parsed");
        CHECK(!loc.hasFix, "151: has a fix");
        CHECK(loc.resultCode == 16, "151: result %d", loc.resultCode);
    }
    // an empty document: a valid IPv4/UDP/LRRP header with no position and no result
    {
        std::vector<uint8_t> d = hex("4500001E00000000401100000C0000970D00FAFA0FA10FA1000A00000D00");
        DmrLocation loc;
        CHECK(!parseDmrLrrp(d.data(), d.size(), loc), "empty document parsed as LRRP");
    }
    // not an IP packet
    {
        std::vector<uint8_t> d(60, 0x00);
        DmrLocation loc;
        CHECK(!parseDmrLrrp(d.data(), d.size(), loc), "zeros parsed as LRRP");
    }
    // a short buffer
    {
        std::vector<uint8_t> d(4, 0x00);
        DmrLocation loc;
        CHECK(!parseDmrLrrp(d.data(), d.size(), loc), "short buffer parsed");
    }
    if (fails) { printf("%d lrrp checks failed\n", fails); return 1; }
    printf("lrrp ok\n");
    return 0;
}
