#pragma once
#include <atomic>
#include <cstdint>
#include <functional>

namespace audio {

constexpr uint32_t kRate = 48000;
constexpr uint32_t kChannels = 6;

// channel order of the frames the mixer renders
enum Speaker : uint32_t {
    FrontLeft,
    FrontRight,
    Centre,
    Sub,
    BackLeft,
    BackRight,
};

// starts the platform output, which pulls interleaved float frames of kChannels from render.
// Returns once the device is open and IsSurround is known.
void StartOutput(std::function<void(float* out, uint32_t frames)> render);

// the mix is 5.1: by the audio output setting, or on automatic when the default device has 5.1 speakers
bool IsSurround();
// the audio output setting is mono: the stereo mix plays as one channel on the front pair
bool IsMono();

// audio output setting, any thread; a change applies at once
constexpr uint32_t kOutputAuto = 0;
constexpr uint32_t kOutputStereo = 1;
constexpr uint32_t kOutputSurround = 2;
constexpr uint32_t kOutputMono = 3;
void SetOutputMode(uint32_t mode);
// any thread: paused, the output stops and the mixer is not run, so every voice and stream keeps its place
void SetPaused(bool paused);
// total stereo frames handed to the output device so far, any thread: the clock films pace on
uint64_t FramesOut();

// Streams follow the picture: when the title has not shown a frame for kStallHoldMs, the mixer holds every stream where
// it is (its voices and music stand still) and goes on when the picture does, so a stutter does not leave the voice
// ahead of the cutscene. Not while a film plays (XMV shows its frames through the overlay, not through Swap).
constexpr int64_t kStallHoldMs = 100;      // the one number to tune: lower holds sooner, 0 never holds
inline std::atomic<int64_t> g_swap_ms{0};  // steady clock ms of the title's last D3DDevice_Swap, 0 before the first
inline std::atomic<bool> g_film_on{false};  // a film plays through the overlay

}  // namespace audio
