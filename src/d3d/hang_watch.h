// Where the game stands when it stops presenting frames: written by the title's thread (in Swap) and by the render
// thread (the command it runs), read by a watchdog thread that logs them while no frame comes out.
#pragma once
#include <atomic>
#include <cstdint>

namespace hang {
inline std::atomic<int> render_op{-1};       // Op the render thread is running, -1 when it waits for work
inline std::atomic<uint64_t> render_cmds{0};  // commands the render thread has run
inline std::atomic<int64_t> last_swap_ms{0};  // steady clock time of the last Swap
inline std::atomic<const char*> game_where{"game code"};
}  // namespace hang
