// What the OmniCoreSDR driver (core/src/source/source_omnicore.cpp) reports of a radio while it receives, for the interface.
#pragma once
#include <string>

namespace dect2 {

struct OmniCoreStatus {
    bool receiving = false;    // a receive stream of this program runs on the radio
    bool rssiOn = false;       // the "RX RSSI" radio setting is on
    bool rssiKnown = false;    // a reading came back (OCH 0.4.7 or later, and a firmware that measures)
    int input = 0;             // the RX input in use: 0 = RX1, 1 = RX2
    // the AD9361's symbol RSSI of both receivers, in relative dB (higher is stronger; NOT calibrated input power). valid: the radio
    // measured (it does not while its state machine is not receiving)
    bool valid[2] = {false, false};
    double rssiDb[2] = {0, 0};
    std::string note;          // why there is no reading, in plain words; empty otherwise
};

// The status of the OmniCoreSDR with this serial (DeviceInfo::serial); a default OmniCoreStatus when it is not receiving here.
OmniCoreStatus omnicoreStatus(const std::string& serial);

} // namespace dect2
