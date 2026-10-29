// Digital video generator: encoder list, capabilities, bitrate estimate and the saved settings (videogen_params.h).
#include "dect2/tv/videogen_params.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
}
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string_view>

namespace dect2::vgen {

namespace {

struct Candidate { const char* name; Codec codec; bool hw; };
// best first within a codec; the software encoders before the hardware ones of the same codec
const Candidate kVideo[] = {
    {"mpeg2video", Codec::Mpeg2, false},
    {"libx264", Codec::H264, false}, {"h264_nvenc", Codec::H264, true}, {"h264_qsv", Codec::H264, true}, {"h264_amf", Codec::H264, true},
    {"h264_videotoolbox", Codec::H264, true}, {"h264_mf", Codec::H264, true}, {"h264_vaapi", Codec::H264, true}, {"libopenh264", Codec::H264, false},
    {"libx265", Codec::Hevc, false}, {"hevc_nvenc", Codec::Hevc, true}, {"hevc_qsv", Codec::Hevc, true}, {"hevc_amf", Codec::Hevc, true},
    {"hevc_videotoolbox", Codec::Hevc, true}, {"hevc_mf", Codec::Hevc, true}, {"hevc_vaapi", Codec::Hevc, true},
};
struct ACandidate { const char* name; AudioCodec codec; };
const ACandidate kAudio[] = {{"aac", AudioCodec::Aac}, {"mp2", AudioCodec::Mp2}, {"ac3", AudioCodec::Ac3}};

bool hasPixFmt(const AVCodec* c, AVPixelFormat want) {
    const AVPixelFormat* f = nullptr;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    if (avcodec_get_supported_config(nullptr, c, AV_CODEC_CONFIG_PIX_FORMAT, 0, (const void**)&f, nullptr) < 0) return false;
#else
    f = c->pix_fmts;
#endif
    if (!f) return true;   // not stated: anything
    for (; *f != AV_PIX_FMT_NONE; f++) if (*f == want) return true;
    return false;
}

// An encoder counts only when it opens: a name in the build is not enough (nvenc without an NVIDIA card, VAAPI without frames on the GPU)
bool videoOpens(const AVCodec* c) {
    const AVPixelFormat pf = hasPixFmt(c, AV_PIX_FMT_YUV420P) ? AV_PIX_FMT_YUV420P : hasPixFmt(c, AV_PIX_FMT_NV12) ? AV_PIX_FMT_NV12 : AV_PIX_FMT_NONE;
    if (pf == AV_PIX_FMT_NONE) return false;
    AVCodecContext* cx = avcodec_alloc_context3(c);
    if (!cx) return false;
    cx->width = 320; cx->height = 240; cx->pix_fmt = pf;
    cx->time_base = AVRational{1, 25}; cx->framerate = AVRational{25, 1};
    cx->bit_rate = 500000; cx->gop_size = 25;
    AVDictionary* o = nullptr;
    if (strcmp(c->name, "libx265") == 0) av_dict_set(&o, "x265-params", "log-level=none", 0);
    const bool ok = avcodec_open2(cx, c, &o) >= 0;
    av_dict_free(&o);
    avcodec_free_context(&cx);
    return ok;
}

bool audioOpens(const AVCodec* c) {
    const AVSampleFormat* f = nullptr;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    if (avcodec_get_supported_config(nullptr, c, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, (const void**)&f, nullptr) < 0) return false;
#else
    f = c->sample_fmts;
#endif
    AVCodecContext* cx = avcodec_alloc_context3(c);
    if (!cx) return false;
    cx->sample_fmt = f ? f[0] : AV_SAMPLE_FMT_FLTP;
    cx->sample_rate = 48000; cx->bit_rate = 96000;
    av_channel_layout_default(&cx->ch_layout, 1);
    cx->time_base = AVRational{1, 48000};
    const bool ok = avcodec_open2(cx, c, nullptr) >= 0;
    avcodec_free_context(&cx);
    return ok;
}

bool starts(std::string_view s, std::string_view p) { return s.starts_with(p); }

std::string kbps(double v) {
    char b[48];
    if (v >= 10000) snprintf(b, sizeof b, "%.2f Mbit/s", v / 1000.0);
    else snprintf(b, sizeof b, "%.0f kbit/s", v);
    return b;
}

std::string fpsText(double f) {
    char b[32];
    if (std::fabs(f - std::round(f)) < 1e-6) snprintf(b, sizeof b, "%.0f fps", f);
    else snprintf(b, sizeof b, "%.1f fps", f);
    return b;
}

} // namespace

Codec codecOf(const std::string& e) {
    if (e == "libx264" || e == "libopenh264" || starts(e, "h264_")) return Codec::H264;
    if (e == "libx265" || starts(e, "hevc_")) return Codec::Hevc;
    return Codec::Mpeg2;
}

AudioCodec audioCodecOf(const std::string& e) {
    if (e == "aac") return AudioCodec::Aac;
    if (e == "mp2" || e == "mp2fixed") return AudioCodec::Mp2;
    if (e == "ac3" || e == "ac3_fixed") return AudioCodec::Ac3;
    return AudioCodec::None;
}

EncoderCaps capsOf(const std::string& e) {
    EncoderCaps c;
    if (e == "libx264") { c.preset = c.zeroLatency = c.profile = c.bFrames = c.crf = true; }
    else if (e == "libx265") { c.preset = c.zeroLatency = c.bFrames = c.crf = true; }   // x265's profiles are main / main10, not the H.264 ones
    else if (e == "mpeg2video") { c.bFrames = true; }
    else if (e == "libopenh264") { c.profile = true; }
    else if (codecOf(e) == Codec::H264) { c.profile = c.bFrames = true; }
    else if (codecOf(e) == Codec::Hevc) { c.bFrames = true; }
    return c;
}

const char* codecName(Codec c) {
    switch (c) {
    case Codec::H264: return "H.264";
    case Codec::Hevc: return "H.265";
    default: return "MPEG-2";
    }
}

const char* audioCodecName(AudioCodec c) {
    switch (c) {
    case AudioCodec::Aac: return "AAC";
    case AudioCodec::Mp2: return "MPEG-1 Layer II";
    case AudioCodec::Ac3: return "AC-3";
    default: return "-";
    }
}

const std::vector<EncoderInfo>& videoEncoders() {
    static const std::vector<EncoderInfo> list = [] {
        std::vector<EncoderInfo> v;
        for (const Candidate& k : kVideo) {
            const AVCodec* c = avcodec_find_encoder_by_name(k.name);
            if (!c || !videoOpens(c)) continue;
            EncoderInfo e;
            e.name = k.name; e.codec = k.codec; e.hardware = k.hw;
            e.label = std::string(codecName(k.codec)) + " (" + k.name + ")";
            v.push_back(e);
        }
        return v;
    }();
    return list;
}

const std::vector<EncoderInfo>& audioEncoders() {
    static const std::vector<EncoderInfo> list = [] {
        std::vector<EncoderInfo> v;
        for (const ACandidate& k : kAudio) {
            const AVCodec* c = avcodec_find_encoder_by_name(k.name);
            if (!c || !audioOpens(c)) continue;
            EncoderInfo e;
            e.name = k.name; e.audio = k.codec;
            e.label = audioCodecName(k.codec);
            v.push_back(e);
        }
        return v;
    }();
    return list;
}

std::string pickVideoEncoder(const std::string& wanted, const std::vector<EncoderInfo>& have) {
    for (const EncoderInfo& e : have) if (e.name == wanted) return wanted;
    for (const EncoderInfo& e : have) if (e.codec == codecOf(wanted)) return e.name;
    return "mpeg2video";
}

int audioTrackCount(const Settings& s) {
    if (s.content == TsContent::VideoOnly || s.audioEncoder.empty()) return 0;
    if (s.content == TsContent::VideoAudio) return 1;
    return std::clamp(s.audioTracks, 2, 4);
}

double bitsPerPixel(Codec c, FrameTypes f) {
    // tuned so the spec's defaults (H.264, 720x576, 15 fps, I/P/B) come to 1 Mbit/s; HEVC needs about 30 % less, MPEG-2 about twice as much
    double b = c == Codec::H264 ? 0.161 : c == Codec::Hevc ? 0.113 : 0.32;
    if (f == FrameTypes::IP) b *= 1.15;
    else if (f == FrameTypes::IOnly) b *= c == Codec::Mpeg2 ? 2.5 : 3.0;   // no prediction at all: the modern codecs lose most
    return b;
}

int macroblocks(int width, int height) { return ((std::max(1, width) + 15) / 16) * ((std::max(1, height) + 15) / 16); }

int minVideoKbps(const Settings& s) {
    if (codecOf(s.videoEncoder) != Codec::Mpeg2) return 10;
    const int64_t bits = (int64_t)macroblocks(s.width, s.height) * kMpeg2MinBitsPerMb;
    return std::max(10, (int)((bits + 9999) / 10000 * 10));
}

bool mpeg2CbrHeaders(int width, int height, int64_t bitrate) { return bitrate >= (int64_t)macroblocks(width, height) * kMpeg2CbrBitsPerMb; }

int suggestVideoKbps(const Settings& s) {
    const double kb = bitsPerPixel(codecOf(s.videoEncoder), s.frames) * s.width * s.height * std::max(0.1, s.fps) / 1000.0;
    return std::max({50, (int)std::lround(kb / 10.0) * 10, minVideoKbps(s)});
}

int effectiveVideoKbps(const Settings& s) { return std::max(minVideoKbps(s), s.autoBitrate ? suggestVideoKbps(s) : s.videoKbps); }

Budget estimate(const Settings& s) {
    Budget b;
    const double fps = std::max(0.1, s.fps);
    b.video = effectiveVideoKbps(s);
    b.videoPeak = s.rc == RateControl::Cbr ? b.video : s.rc == RateControl::ConstrainedVbr ? b.video * 1.5 : b.video * 2.0;
    const int tracks = audioTrackCount(s);
    b.audio = (double)tracks * s.audioKbps;
    // A TS packet carries 184 bytes of payload behind its 4-byte header. Each video picture is one PES (19 header bytes with PTS and DTS)
    // and its last packet is half empty on average (stuffing in the adaptation field). The muxer sends audio PES of about 2930 bytes,
    // at least every 0.5 s. The PCR (every 40 ms) takes 8 bytes of adaptation field.
    auto ts = [](double kbit) { return kbit * 4.0 / 184.0; };
    const double vPes = fps * (19 + 92) * 8 / 1000.0;
    double aPes = 0;
    if (tracks > 0) {
        const double pesPerSec = std::max(2.0, s.audioKbps * 1000.0 / 8.0 / 2930.0);
        aPes = tracks * pesPerSec * (14 + 92) * 8 / 1000.0;
    }
    const double pcr = 25 * 8 * 8 / 1000.0;
    b.pesTs = ts(b.video) + ts(b.audio) + vPes + aPes + pcr;
    // PAT and PMT 10 times a second, SDT twice (the periods the generator sets in the muxer)
    b.psiSi = (10 + 10 + 2) * 188 * 8 / 1000.0;
    b.total = b.video + b.audio + b.pesTs + b.psiSi;
    b.peak = b.videoPeak + b.audio + ts(b.videoPeak) + ts(b.audio) + vPes + aPes + pcr + b.psiSi;
    b.suggestedMux = (int)std::ceil(b.peak * 1.03 / 10.0) * 10;
    b.mux = s.autoMux ? b.suggestedMux : std::max(100, s.muxKbps);
    return b;
}

std::vector<std::string> checkSettings(const Settings& s, const Budget& b) {
    std::vector<std::string> w;
    if (b.mux < b.peak) w.push_back("The mux rate is below what the streams need: the muxer will run late and players may stutter.");
    if (s.width % 2 || s.height % 2) w.push_back("Width and height must be even for 4:2:0.");
    if (s.frames == FrameTypes::IPB && !capsOf(s.videoEncoder).bFrames) w.push_back("This encoder makes no B-frames: I/P is used.");
    if (s.profile == 0 && s.frames == FrameTypes::IPB && capsOf(s.videoEncoder).profile) w.push_back("The Baseline profile has no B-frames: I/P is used.");
    if (s.rc == RateControl::Crf && !capsOf(s.videoEncoder).crf) w.push_back("This encoder has no CRF mode: constrained VBR is used.");
    if (codecOf(s.videoEncoder) == Codec::Mpeg2 && (s.width > 1920 || s.height > 1152)) w.push_back("MPEG-2 Main Profile at High Level stops at 1920x1152.");
    if (!s.autoBitrate && s.videoKbps < minVideoKbps(s)) w.push_back("The video bitrate is below the MPEG-2 minimum for this picture size: the minimum is used.");
    return w;
}

const char* paramLabel(Param p) {
    switch (p) {
    case Param::VideoCodec: return "Video codec";
    case Param::VideoBitrate: return "Video bitrate";
    case Param::Resolution: return "Resolution";
    case Param::FrameRate: return "Frame rate";
    case Param::Gop: return "GOP";
    case Param::FrameTypes: return "Frame types";
    case Param::Profile: return "Profile";
    case Param::RateControl: return "Rate control";
    case Param::PixelFormat: return "Pixel format";
    case Param::Preset: return "Encoder preset";
    case Param::Latency: return "Latency tuning";
    case Param::AudioCodec: return "Audio codec";
    case Param::AudioBitrate: return "Audio bitrate";
    case Param::SampleRate: return "Sample rate";
    case Param::Channels: return "Audio channels";
    case Param::TsContent: return "TS content";
    case Param::MuxRate: return "TS mux rate";
    case Param::TotalRate: return "Total TS rate";
    case Param::Service: return "Service";
    default: return "";
    }
}

std::string paramValue(Param p, const Settings& s) {
    char b[128];
    const Budget bu = estimate(s);
    switch (p) {
    case Param::VideoCodec: return std::string(codecName(codecOf(s.videoEncoder))) + " (" + s.videoEncoder + ")";
    case Param::VideoBitrate: return s.rc == RateControl::Crf && capsOf(s.videoEncoder).crf ? "CRF " + std::to_string(s.crf) : kbps(bu.video);
    case Param::Resolution: snprintf(b, sizeof b, "%dx%d", s.width, s.height); return b;
    case Param::FrameRate: return fpsText(s.fps);
    case Param::Gop:
        if (s.frames == FrameTypes::IOnly) return "all-intra";
        snprintf(b, sizeof b, "%.1f s (%d frames)", s.gopSeconds, std::max(1, (int)std::lround(s.gopSeconds * s.fps)));
        return b;
    case Param::FrameTypes: return s.frames == FrameTypes::IPB ? "I/P/B" : s.frames == FrameTypes::IP ? "I/P" : "I-only";
    case Param::Profile: return capsOf(s.videoEncoder).profile ? kProfiles[std::clamp(s.profile, 0, kNumProfiles - 1)] : "-";
    case Param::RateControl: return s.rc == RateControl::Cbr ? "CBR" : s.rc == RateControl::ConstrainedVbr ? "Constrained VBR" : "VBR/CRF";
    case Param::PixelFormat: return "yuv420p 8-bit";
    case Param::Preset: return capsOf(s.videoEncoder).preset ? kPresets[std::clamp(s.preset, 0, kNumPresets - 1)] : "-";
    case Param::Latency: return s.zeroLatency && capsOf(s.videoEncoder).zeroLatency ? "zerolatency" : "default";
    case Param::AudioCodec: return audioTrackCount(s) ? audioCodecName(audioCodecOf(s.audioEncoder)) : "no audio";
    case Param::AudioBitrate: return audioTrackCount(s) ? kbps(s.audioKbps) : "-";
    case Param::SampleRate: snprintf(b, sizeof b, "%.1f kHz", s.sampleRate / 1000.0); return b;
    case Param::Channels: return s.channels >= 2 ? "stereo" : "mono";
    case Param::TsContent: {
        const int n = audioTrackCount(s);
        if (n == 0) return "video only";
        snprintf(b, sizeof b, "video + %d audio", n);
        return b;
    }
    case Param::MuxRate: return kbps(bu.mux);
    case Param::TotalRate: return kbps(bu.total);
    case Param::Service: snprintf(b, sizeof b, "%s (%d)", s.serviceName.c_str(), s.serviceId); return b;
    default: return "";
    }
}

std::string saveSettings(const Settings& s) {
    std::ostringstream o;
    o << "content=" << (int)s.content << "\n" << "venc=" << s.videoEncoder << "\n" << "rc=" << (int)s.rc << "\n"
      << "autobr=" << (s.autoBitrate ? 1 : 0) << "\n" << "vkbps=" << s.videoKbps << "\n" << "crf=" << s.crf << "\n"
      << "fps=" << s.fps << "\n" << "w=" << s.width << "\n" << "h=" << s.height << "\n" << "gop=" << s.gopSeconds << "\n"
      << "frames=" << (int)s.frames << "\n" << "profile=" << s.profile << "\n" << "preset=" << s.preset << "\n"
      << "zerolat=" << (s.zeroLatency ? 1 : 0) << "\n" << "aenc=" << s.audioEncoder << "\n" << "atracks=" << s.audioTracks << "\n"
      << "akbps=" << s.audioKbps << "\n" << "arate=" << s.sampleRate << "\n" << "ach=" << s.channels << "\n"
      << "automux=" << (s.autoMux ? 1 : 0) << "\n" << "mux=" << s.muxKbps << "\n" << "sid=" << s.serviceId << "\n"
      << "autopid=" << (s.autoPids ? 1 : 0) << "\n" << "pmtpid=" << s.pmtPid << "\n" << "vpid=" << s.videoPid << "\n"
      << "apid=" << s.audioPid << "\n" << "sname=" << s.serviceName << "\n" << "sprov=" << s.provider << "\n";
    return o.str();
}

void loadSettings(const std::string& text, Settings& s) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        const int i = atoi(v.c_str());
        const double d = atof(v.c_str());
        if (k == "content") s.content = (TsContent)std::clamp(i, 0, 2);
        else if (k == "venc" && !v.empty()) s.videoEncoder = v;
        else if (k == "rc") s.rc = (RateControl)std::clamp(i, 0, 2);
        else if (k == "autobr") s.autoBitrate = i != 0;
        else if (k == "vkbps") s.videoKbps = std::clamp(i, 10, 100000);
        else if (k == "crf") s.crf = std::clamp(i, 0, 51);
        else if (k == "fps") s.fps = std::clamp(d, 1.0, 60.0);
        else if (k == "w") s.width = std::clamp(i, 16, 3840) & ~1;
        else if (k == "h") s.height = std::clamp(i, 16, 2160) & ~1;
        else if (k == "gop") s.gopSeconds = std::clamp(d, 0.1, 10.0);
        else if (k == "frames") s.frames = (FrameTypes)std::clamp(i, 0, 2);
        else if (k == "profile") s.profile = std::clamp(i, 0, kNumProfiles - 1);
        else if (k == "preset") s.preset = std::clamp(i, 0, kNumPresets - 1);
        else if (k == "zerolat") s.zeroLatency = i != 0;
        else if (k == "aenc") s.audioEncoder = v;
        else if (k == "atracks") s.audioTracks = std::clamp(i, 2, 4);
        else if (k == "akbps") s.audioKbps = std::clamp(i, 16, 640);
        else if (k == "arate") s.sampleRate = std::clamp(i, 8000, 48000);
        else if (k == "ach") s.channels = std::clamp(i, 1, 2);
        else if (k == "automux") s.autoMux = i != 0;
        else if (k == "mux") s.muxKbps = std::clamp(i, 100, 200000);
        else if (k == "sid") s.serviceId = std::clamp(i, 1, 65535);
        else if (k == "autopid") s.autoPids = i != 0;
        else if (k == "pmtpid") s.pmtPid = std::clamp(i, 0x20, 0x1FFE);
        else if (k == "vpid") s.videoPid = std::clamp(i, 0x20, 0x1FFE);
        else if (k == "apid") s.audioPid = std::clamp(i, 0x20, 0x1FFA);
        else if (k == "sname") s.serviceName = v;
        else if (k == "sprov") s.provider = v;
    }
}

} // namespace dect2::vgen
