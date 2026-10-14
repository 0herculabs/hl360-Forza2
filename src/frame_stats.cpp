// Frame pacing statistics (opt-in: --frame_stats=true).
//
// Every frame the title ends in 0x8236CB28, the XDK Present path that calls
// VdSwap. Wrapping it gives the real frame interval as the game sees it.
// Every 5 seconds a summary goes to the log: fps, average / 99th percentile /
// worst frame time, and how many frames missed 1/60 s and 1/30 s.
#include <algorithm>
#include <chrono>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

REXCVAR_DEFINE_BOOL(frame_stats, false, "Profile", "Log frame pacing statistics every 5 seconds");

extern "C" void __imp__sub_8236CB28(PPCContext& __restrict ctx, uint8_t* base);

namespace {

using Clock = std::chrono::steady_clock;

struct Stats {
  Clock::time_point window_start{};
  Clock::time_point last{};
  std::vector<float> frame_ms;
};

}  // namespace

extern "C" void sub_8236CB28(PPCContext& __restrict ctx, uint8_t* base) {
  if (!REXCVAR_GET(frame_stats)) {
    __imp__sub_8236CB28(ctx, base);
    return;
  }
  static Stats stats;
  const auto now = Clock::now();
  if (stats.last.time_since_epoch().count()) {
    stats.frame_ms.push_back(std::chrono::duration<float, std::milli>(now - stats.last).count());
  } else {
    stats.window_start = now;
  }
  stats.last = now;

  const float window_s = std::chrono::duration<float>(now - stats.window_start).count();
  if (window_s >= 5.0f && !stats.frame_ms.empty()) {
    auto sorted = stats.frame_ms;
    std::sort(sorted.begin(), sorted.end());
    float sum = 0;
    int over60 = 0, over30 = 0;
    for (float ms : sorted) {
      sum += ms;
      over60 += ms > 17.5f;
      over30 += ms > 34.0f;
    }
    const size_t n = sorted.size();
    REXLOG_WARN(
        "frame stats: {} frames in {:.1f}s = {:.1f} fps | avg {:.2f} ms, p99 {:.2f} ms, max {:.2f} ms | "
        ">1/60s: {} | >1/30s: {}",
        n, window_s, float(n) / window_s, sum / float(n), sorted[std::min(n - 1, n * 99 / 100)],
        sorted.back(), over60, over30);
    stats.frame_ms.clear();
    stats.window_start = now;
  }
  __imp__sub_8236CB28(ctx, base);
}

