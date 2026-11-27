// atracdenc's PCM input, in place of its libsndfile / Media Foundation
// backends: reads only the 16-bit WAV that snd0.cpp writes.
// LGPL-2.1-or-later, as atracdenc.
#include "wav.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {

class TPcmWav : public IPCMProviderImpl {
public:
    explicit TPcmWav(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        uint8_t h[44];
        if (!f || fread(h, 1, 44, f) != 44 || memcmp(h, "RIFF", 4) || memcmp(h + 36, "data", 4)) {
            if (f) fclose(f);
            throw std::runtime_error("cannot read " + path);
        }
        Channels = h[22];
        Rate = h[24] | h[25] << 8 | h[26] << 16 | static_cast<uint32_t>(h[27]) << 24;
        uint8_t b[2];
        while (fread(b, 1, 2, f) == 2)
            Pcm.push_back(static_cast<int16_t>(b[0] | b[1] << 8));
        fclose(f);
    }
    size_t GetChannelsNum() const override { return Channels; }
    size_t GetSampleRate() const override { return Rate; }
    size_t GetTotalSamples() const override { return Pcm.size() / Channels; }
    size_t Read(TPCMBuffer& buf, size_t sz) override {
        size_t n = GetTotalSamples() - Pos;
        if (n > sz) n = sz;
        for (size_t i = 0; i < n; i++)
            for (size_t c = 0; c < Channels; c++)
                buf[i][c] = Pcm[(Pos + i) * Channels + c] / 32768.0f;
        Pos += n;
        return n;
    }
    size_t Write(const TPCMBuffer&, size_t) override { throw std::runtime_error("no PCM output"); }

private:
    std::vector<int16_t> Pcm;
    size_t Channels = 0, Rate = 0, Pos = 0;
};

}

IPCMProviderImpl* CreatePCMIOReadImpl(const std::string& path) {
    return new TPcmWav(path);
}

IPCMProviderImpl* CreatePCMIOWriteImpl(const std::string&, int, int) {
    throw std::runtime_error("no PCM output");
}
