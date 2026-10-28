// mixer output through an SDL audio stream (AAudio underneath): the 5.1 mix folded to the stereo pair Android plays
#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

#include "audio/output.h"
#include "core/log.h"

namespace audio {

namespace {

constexpr float kCentreFold = 0.707f;
constexpr uint32_t kBlock = kRate / 100;  // frames the mixer renders a call: 10 ms

std::atomic<uint32_t> g_mode{kOutputAuto};
std::atomic<bool> g_paused{false};
std::atomic<uint64_t> g_frames_out{0};
SDL_AudioStream* g_stream = nullptr;

struct Pump {
    std::function<void(float*, uint32_t)> render;
    std::vector<float> mix = std::vector<float>(kBlock * kChannels);
    std::vector<float> out;  // stereo frames rendered, not yet handed to SDL
};

// L = FL + kCentreFold C + BL and R = FR + kCentreFold C + BR without the sub; mono hears the average of the two
void Fold(const float* in, uint32_t frames, bool mono, std::vector<float>& out) {
    for (uint32_t i = 0; i < frames; i++) {
        const float* f = in + size_t(i) * kChannels;
        float c = f[Centre] * kCentreFold;
        float l = f[FrontLeft] + c + f[BackLeft];
        float r = f[FrontRight] + c + f[BackRight];
        if (mono) l = r = (l + r) * 0.5f;
        out.push_back(l);
        out.push_back(r);
    }
}

void SDLCALL Feed(void* user, SDL_AudioStream* stream, int additional, int) {
    using clock = std::chrono::steady_clock;
    const clock::time_point t0 = clock::now();
    auto& p = *static_cast<Pump*>(user);
    constexpr int kFrameBytes = 2 * sizeof(float);
    size_t want = size_t(std::max(additional, 0) / kFrameBytes) * 2;
    while (p.out.size() < want) {
        p.render(p.mix.data(), kBlock);
        Fold(p.mix.data(), kBlock, g_mode == kOutputMono, p.out);
    }
    if (!want) return;
    SDL_PutAudioStreamData(stream, p.out.data(), int(want * sizeof(float)));
    g_frames_out += want / 2;  // 2 floats per stereo frame
    p.out.erase(p.out.begin(), p.out.begin() + std::ptrdiff_t(want));

    // DIAGNOSTIC (no change in behaviour): does the audio keep up with the wall clock? Every 5 s one line:
    // handed = audio seconds given to SDL so far, wall = real seconds since the first call. handed falling behind wall
    // and the gap growing means the mixer was starved and the sound slipped behind the picture.
    static clock::time_point start = t0, last = t0;
    static double max_ms = 0;
    static int min_queued_ms = 1 << 30;
    max_ms = std::max(max_ms, std::chrono::duration<double, std::milli>(clock::now() - t0).count());
    min_queued_ms = std::min(min_queued_ms, int(int64_t(SDL_GetAudioStreamQueued(stream)) / kFrameBytes * 1000 / int(kRate)));
    if (t0 - last >= std::chrono::seconds(5)) {
        double wall = std::chrono::duration<double>(t0 - start).count();
        double handed = double(g_frames_out.load()) / double(kRate);
        LOGI("audio: handed %.2f s, wall %.2f s, behind %.0f ms, queue min %d ms, mixer max %.1f ms per call", handed, wall,
             (wall - handed) * 1000.0, min_queued_ms, max_ms);
        last = t0;
        max_ms = 0;
        min_queued_ms = 1 << 30;
    }
}

}  // namespace

// a 5.1 mix only when the player asks for it: phones and tablets play stereo
bool IsSurround() { return g_mode == kOutputSurround; }

bool IsMono() { return g_mode == kOutputMono; }

uint64_t FramesOut() { return g_frames_out.load(); }

void SetOutputMode(uint32_t mode) {
    if (g_mode.exchange(mode) != mode) LOGI("audio: output %u", mode);
}

void SetPaused(bool paused) {
    if (g_paused.exchange(paused) == paused || !g_stream) return;
    if (paused) SDL_PauseAudioStreamDevice(g_stream);
    else SDL_ResumeAudioStreamDevice(g_stream);
}

void StartOutput(std::function<void(float*, uint32_t)> render) {
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        LOGE("audio: SDL audio unavailable, no sound: %s", SDL_GetError());
        return;
    }
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, int(kRate)};
    auto* pump = new Pump{std::move(render)};
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Feed, pump);
    if (!g_stream) {
        LOGE("audio: no output device, no sound: %s", SDL_GetError());
        delete pump;
        return;
    }
    LOGI("audio: %s output through %s", IsSurround() ? "5.1 folded to stereo" : "stereo", SDL_GetCurrentAudioDriver());
    if (!g_paused) SDL_ResumeAudioStreamDevice(g_stream);
}

}  // namespace audio
