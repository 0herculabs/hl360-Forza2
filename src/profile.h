#pragma once

#include <filesystem>
#include <string>

namespace forza2 {

// The signed-in profile's name, from the `gamertag` cvar, sanitized to the
// Xbox 360 gamertag rules (falls back to "Player").
std::string Gamertag();

// Per-profile user-data folder under `base`: base/profiles/<gamertag>.
std::filesystem::path ProfileUserDataRoot(const std::filesystem::path& base);

// Logs the guest call chain on an unrecovered access violation (crash_report.cpp).
void InstallCrashReporter();

// Drops the per-frame empty-resolve GPU log spam (log_filter.cpp).
void InstallGpuLogFilter();

// Shader warm-up mode for the launcher (shader_warmup.cpp).
bool ShaderWarmupMode();
void HideOwnWindows();

// Installs the packages in <game_root>/dlc the profile at `user_root` lacks (dlc.cpp).
void InstallBundledDlc(const std::filesystem::path& game_root, const std::filesystem::path& user_root);


}  // namespace forza2
