// Drop two GPU log lines Forza 2 triggers every frame.
//
// The title issues EDRAM copies (edram_mode 6) whose resolve rectangle is
// empty. The runtime correctly skips them, but logs each one as an error -
// "Resolve region is empty" plus "...: Failed in backend (... edram_mode=6)" -
// about 1,400 lines a second, written to disk from the GPU thread. That is
// enough to cause hitches: in a race at 120 Hz it held 64-105 fps with the
// spam and 111-115 fps without it. Every other GPU message still gets through.
#include <memory>
#include <string_view>

#include <spdlog/logger.h>
#include <spdlog/sinks/sink.h>

#include <rex/logging.h>
#include <rex/logging/api.h>

namespace {

class SkipEmptyResolveSink : public spdlog::sinks::sink {
 public:
  explicit SkipEmptyResolveSink(spdlog::sink_ptr inner) : inner_(std::move(inner)) {}

  void log(const spdlog::details::log_msg& msg) override {
    const std::string_view text(msg.payload.data(), msg.payload.size());
    if (text == "Resolve region is empty") return;
    if (text.find("Failed in backend") != std::string_view::npos &&
        text.find("edram_mode=6)") != std::string_view::npos) {
      return;
    }
    inner_->log(msg);
  }
  void flush() override { inner_->flush(); }
  void set_pattern(const std::string& pattern) override { inner_->set_pattern(pattern); }
  void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override {
    inner_->set_formatter(std::move(formatter));
  }

 private:
  spdlog::sink_ptr inner_;
};

}  // namespace

namespace forza2 {

// Wraps every "gpu" logger sink not wrapped yet; safe to call again.
void InstallGpuLogFilter() {
  const auto category = rex::FindCategory("gpu");
  if (!category) return;
  auto logger = rex::GetLogger(*category);
  if (!logger) return;
  int wrapped = 0, already = 0;
  for (auto& sink : logger->sinks()) {
    if (std::dynamic_pointer_cast<SkipEmptyResolveSink>(sink)) {
      ++already;
    } else {
      sink = std::make_shared<SkipEmptyResolveSink>(sink);
      ++wrapped;
    }
  }
  REXLOG_INFO("gpu log filter: wrapped {} sinks, {} already wrapped", wrapped, already);
}

}  // namespace forza2
