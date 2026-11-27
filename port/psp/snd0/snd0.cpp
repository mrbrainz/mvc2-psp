// mvc2-snd0 IN.ADX OUT.AT3: a looping CRI ADX track (the character select
// music, AFS00 file 15) as the XMB's SND0.AT3: ATRAC3 132 kbps, 44.1 kHz, in
// RIFF with the loop in a smpl chunk. Writes OUT.AT3.wav while it works.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "adx.h"
}

int main_(int argc, char* const* argv);   // atracdenc (main.cpp)

namespace {

const int OutRate = 44100;
const int Tail = 4096;          // samples past the loop end, taken from the loop start
const int FactDelay = 1024;     // what atracdenc writes in "fact"
const int LoopShift = 1024 + 69;   // + the ATRAC3 decoder's own delay
const size_t MaxBytes = 500 * 1024;   // the XMB's limit

int fail(const char* msg) {
    fprintf(stderr, "mvc2-snd0: %s\n", msg);
    return 1;
}

std::vector<uint8_t> readFile(const char* path) {
    std::vector<uint8_t> d;
    if (FILE* f = fopen(path, "rb")) {
        uint8_t b[65536];
        for (size_t n; (n = fread(b, 1, sizeof b, f)) > 0;)
            d.insert(d.end(), b, b + n);
        fclose(f);
    }
    return d;
}

bool writeFile(const std::string& path, const std::vector<uint8_t>& d) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(d.data(), 1, d.size(), f) == d.size();
    return !fclose(f) && ok;
}

void put32(std::vector<uint8_t>& d, uint32_t v) {
    for (int i = 0; i < 4; i++) d.push_back(static_cast<uint8_t>(v >> 8 * i));
}

void set32(std::vector<uint8_t>& d, size_t at, uint32_t v) {
    for (int i = 0; i < 4; i++) d[at + i] = static_cast<uint8_t>(v >> 8 * i);
}

// windowed-sinc resampling of interleaved stereo to `frames` frames
std::vector<int16_t> resample(const std::vector<int16_t>& in, uint32_t rate, size_t frames) {
    const double pi = 3.14159265358979323846;
    const double step = static_cast<double>(rate) / OutRate;
    const double cut = std::min(1.0, 1.0 / step) * 0.97;
    const int half = 32;
    const long n = static_cast<long>(in.size() / 2);
    std::vector<int16_t> out(frames * 2);
    for (size_t i = 0; i < frames; i++) {
        const double t = i * step;
        const long k0 = static_cast<long>(t);
        double sum[2] = {0, 0}, weight = 0;
        for (long k = k0 - half + 1; k <= k0 + half; k++) {
            const double x = t - k, px = pi * cut * x;
            const double w = (x == 0 ? 1.0 : std::sin(px) / px) *
                             (0.42 + 0.5 * std::cos(pi * x / half) + 0.08 * std::cos(2 * pi * x / half));
            weight += w;
            if (k >= 0 && k < n)
                for (int c = 0; c < 2; c++) sum[c] += w * in[k * 2 + c];
        }
        for (int c = 0; c < 2; c++)
            out[i * 2 + c] = static_cast<int16_t>(std::max(-32768.0, std::min(32767.0, std::round(sum[c] / weight))));
    }
    return out;
}

}

int main(int argc, char** argv) {
    if (argc != 3) return fail("usage: mvc2-snd0 IN.ADX OUT.AT3");
    const std::vector<uint8_t> adx = readFile(argv[1]);
    static AdxStream s;
    if (adx.empty() || adx_open_mem(&s, adx.data(), adx.size(), 1) || !s.loop || s.h.channels != 2)
        return fail("not a looping stereo ADX track");

    // one pass, then on into the loop, so the encoder sees what follows the end
    const size_t want = s.h.loop_end + 8192;
    std::vector<int16_t> pcm(want * 2);
    if (adx_decode(&s, pcm.data(), static_cast<int>(want)) != static_cast<int>(want))
        return fail("the ADX track ends early");

    const double ratio = static_cast<double>(OutRate) / s.h.rate;
    const uint32_t start = static_cast<uint32_t>(s.h.loop_start * ratio);
    const uint32_t end = start + static_cast<uint32_t>(std::llround((s.h.loop_end - s.h.loop_start) * ratio));
    const std::vector<int16_t> out = resample(pcm, s.h.rate, end + Tail);

    const std::string outPath = argv[2], wavPath = outPath + ".wav";
    std::vector<uint8_t> wav = {'R', 'I', 'F', 'F'};
    const uint32_t bytes = static_cast<uint32_t>(out.size() * 2);
    put32(wav, bytes + 36);
    for (char c : std::string("WAVEfmt ")) wav.push_back(c);
    put32(wav, 16);
    put32(wav, 1 | 2 << 16);            // PCM, stereo
    put32(wav, OutRate);
    put32(wav, OutRate * 4);
    put32(wav, 4 | 16 << 16);           // block align, bits
    for (char c : std::string("data")) wav.push_back(c);
    put32(wav, bytes);
    for (int16_t v : out) {
        wav.push_back(static_cast<uint8_t>(v));
        wav.push_back(static_cast<uint8_t>(v >> 8));
    }
    if (!writeFile(wavPath, wav)) return fail("cannot write the temporary WAV");

    std::vector<std::string> a = {"atracdenc", "-e", "atrac3", "--container", "riff", "--nostdout",
                                  "-i", wavPath, "-o", outPath};
    std::vector<char*> av;
    for (std::string& x : a) av.push_back(&x[0]);
    av.push_back(nullptr);
    remove(outPath.c_str());
    const int rc = main_(static_cast<int>(a.size()), av.data());
    remove(wavPath.c_str());
    if (rc) return fail("ATRAC3 encoding failed");

    std::vector<uint8_t> at3 = readFile(outPath.c_str());
    size_t fact = 0, data = 0;
    for (size_t p = 12; p + 8 <= at3.size();) {
        const uint32_t len = at3[p + 4] | at3[p + 5] << 8 | at3[p + 6] << 16 | static_cast<uint32_t>(at3[p + 7]) << 24;
        if (!memcmp(&at3[p], "fact", 4) && len == 8) fact = p;
        if (!memcmp(&at3[p], "data", 4)) { data = p; break; }
        p += 8 + len + (len & 1);
    }
    if (!fact || !data || at3[fact + 12] != (FactDelay & 0xFF) || at3[fact + 13] != FactDelay >> 8)
        return fail("unexpected encoder output");
    set32(at3, fact + 8, end + LoopShift - FactDelay);
    std::vector<uint8_t> smpl = {'s', 'm', 'p', 'l'};
    // size; manufacturer, product, period (ns), unity note, pitch, SMPTE x2,
    // one loop, no extra data; loop: id, forward, start, end (inclusive), 0, forever
    for (uint32_t v : {60u, 0u, 0u, 1000000000u / OutRate, 60u, 0u, 0u, 0u, 1u, 0u,
                       0u, 0u, start + LoopShift, end + LoopShift - 1, 0u, 0u})
        put32(smpl, v);
    at3.insert(at3.begin() + data, smpl.begin(), smpl.end());
    set32(at3, 4, static_cast<uint32_t>(at3.size() - 8));
    if (at3.size() > MaxBytes) return fail("the result is too large for the XMB");
    if (!writeFile(outPath, at3)) return fail("cannot write the output");
    return 0;
}
