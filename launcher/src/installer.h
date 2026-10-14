#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace launcher {

// Shared between the worker thread and the UI.
struct TaskProgress {
  enum class State { kIdle, kRunning, kDone, kFailed, kCancelled };
  std::atomic<State> state{State::kIdle};
  std::atomic<uint64_t> done{0};
  std::atomic<uint64_t> total{0};
  std::atomic<bool> cancel{false};

  void SetStatus(std::string text) {
    std::lock_guard lock(mutex);
    status = std::move(text);
  }
  std::string Status() {
    std::lock_guard lock(mutex);
    return status;
  }

  std::mutex mutex;
  std::string status;
};

struct ManifestEntry {
  std::string path;  // '/'-separated, relative to the game folder
  uint64_t size = 0;
  std::string sha256;
  std::vector<std::string> alternatives;  // other accepted contents of the same size
};

// Parses game_files.sha256 (`<sha256> <size> <path>` per line, '#' comments).
// A path listed twice accepts either content: default.xex differs between the
// European and American discs only in its header (media ID, ratings).
std::vector<ManifestEntry> LoadManifest(const std::filesystem::path& file);

enum class GameRegion { kUnknown, kEurope, kUsa };

// Region of a Forza Motorsport 2 default.xex, from the media ID in its header.
GameRegion ReadGameRegion(const std::filesystem::path& xex);

enum class DiscKind { kFailed, kGame, kBonus };

// Extracts an Xbox 360 disc image (XDVDFS, any layout: XGD1/2/3 or a trimmed
// XISO) into `dest`. The game disc is extracted whole, minus $SystemUpdate.
// The Limited Edition bonus disc (ExpansionInstaller.ini) only contributes its
// DLC packages, copied flat to dest/dlc; its title update is left out, since
// it patches default.xex and the port runs the unpatched build. Runs on the
// caller's thread; report through `progress`.
DiscKind ExtractIso(const std::filesystem::path& iso, const std::filesystem::path& dest,
                    TaskProgress& progress);

// DLC packages the bonus disc import left in <game>/dlc.
size_t CountDlcPackages(const std::filesystem::path& game_dir);

struct VerifyReport {
  std::vector<std::string> missing;
  std::vector<std::string> wrong_size;
  std::vector<std::string> wrong_hash;
  bool ok() const { return missing.empty() && wrong_size.empty() && wrong_hash.empty(); }
};

// Checks `dir` against the manifest. full_hash=false only checks presence and
// size (instant); true also hashes every file on all cores.
VerifyReport VerifyGame(const std::filesystem::path& dir, const std::vector<ManifestEntry>& manifest,
                        bool full_hash, TaskProgress& progress);

// Marker written after a full verification passed, so the launcher can trust
// the folder later without re-hashing 5 GB.
std::filesystem::path VerifiedMarker(const std::filesystem::path& game_dir);

}  // namespace launcher
