// Forza2Launcher - profiles, settings and game import for the recompiled
// Forza Motorsport 2. Sits next to forza2.exe; writes forza2.user.toml (read by
// the game after its own forza2.toml) and launches the game.
//
// Layout: the cover art fills a column on the left with the profile picker and
// the play button over it; settings live in tabs on the right, grouped into
// cards. The cover is read from cover.png next to the exe at runtime - it is
// not part of the program.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "forza_plus.h"
#include "i18n.h"
#include "installer.h"
#include "settings.h"

namespace fs = std::filesystem;
using namespace launcher;

namespace {

// Palette, taken from the Forza 2 box art.
const ImVec4 kAccent(0.91f, 0.36f, 0.12f, 1.0f);
const ImVec4 kAccentHover(1.0f, 0.47f, 0.20f, 1.0f);
const ImVec4 kOk(0.36f, 0.80f, 0.45f, 1.0f);
const ImVec4 kWarn(0.98f, 0.74f, 0.25f, 1.0f);
const ImVec4 kBad(0.95f, 0.35f, 0.30f, 1.0f);
const ImVec4 kText(0.93f, 0.93f, 0.95f, 1.0f);
const ImVec4 kDim(0.60f, 0.61f, 0.66f, 1.0f);
const ImVec4 kBg(0.067f, 0.071f, 0.082f, 1.0f);
const ImVec4 kCard(0.105f, 0.110f, 0.125f, 1.0f);
const ImVec4 kControl(0.165f, 0.172f, 0.192f, 1.0f);

float g_scale = 1.0f;
ImFont* g_bold = nullptr;
float S(float v) { return v * g_scale; }
ImU32 Col(const ImVec4& c, float alpha = 1.0f) {
  return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

enum class Tab { kProfiles, kGraphics, kControls, kAudio, kInstall, kForzaPlus, kSupport };

// --- Key capture -----------------------------------------------------------

// SDL scancode -> name understood by the runtime's ParseVirtualKey
// (src/ui/keybinds.cpp in the SDK).
std::string KeyName(SDL_Scancode sc) {
  if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return std::string(1, char('A' + (sc - SDL_SCANCODE_A)));
  if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return std::string(1, char('1' + (sc - SDL_SCANCODE_1)));
  if (sc == SDL_SCANCODE_0) return "0";
  if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return "F" + std::to_string(1 + sc - SDL_SCANCODE_F1);
  if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return "Numpad" + std::to_string(1 + sc - SDL_SCANCODE_KP_1);
  switch (sc) {
    case SDL_SCANCODE_KP_0: return "Numpad0";
    case SDL_SCANCODE_KP_ENTER: return "NumpadEnter";
    case SDL_SCANCODE_KP_PLUS: return "NumpadPlus";
    case SDL_SCANCODE_KP_MINUS: return "NumpadMinus";
    case SDL_SCANCODE_KP_MULTIPLY: return "NumpadStar";
    case SDL_SCANCODE_KP_DIVIDE: return "NumpadSlash";
    case SDL_SCANCODE_RETURN: return "Return";
    case SDL_SCANCODE_SPACE: return "Space";
    case SDL_SCANCODE_TAB: return "Tab";
    case SDL_SCANCODE_BACKSPACE: return "Backspace";
    case SDL_SCANCODE_DELETE: return "Delete";
    case SDL_SCANCODE_INSERT: return "Insert";
    case SDL_SCANCODE_HOME: return "Home";
    case SDL_SCANCODE_END: return "End";
    case SDL_SCANCODE_PAGEUP: return "PageUp";
    case SDL_SCANCODE_PAGEDOWN: return "PageDown";
    case SDL_SCANCODE_LEFT: return "Left";
    case SDL_SCANCODE_RIGHT: return "Right";
    case SDL_SCANCODE_UP: return "Up";
    case SDL_SCANCODE_DOWN: return "Down";
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return "Shift";
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return "Control";
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return "Alt";
    case SDL_SCANCODE_GRAVE: return "Backtick";
    case SDL_SCANCODE_MINUS: return "Minus";
    case SDL_SCANCODE_EQUALS: return "Plus";
    case SDL_SCANCODE_COMMA: return "Comma";
    case SDL_SCANCODE_PERIOD: return "Period";
    case SDL_SCANCODE_SEMICOLON: return "Semicolon";
    case SDL_SCANCODE_SLASH: return "Slash";
    case SDL_SCANCODE_BACKSLASH: return "Backslash";
    case SDL_SCANCODE_LEFTBRACKET: return "LBracket";
    case SDL_SCANCODE_RIGHTBRACKET: return "RBracket";
    case SDL_SCANCODE_APOSTROPHE: return "Quote";
    case SDL_SCANCODE_CAPSLOCK: return "CapsLock";
    default: return {};
  }
}

// Friendlier names for a few runtime key names when shown in chips.
std::string KeyLabel(const std::string& key) {
  const std::string shift = L("Shift+", "Mayús+");
  const std::pair<const char*, std::string> names[] = {
      {"Space", L("Space", "Espacio")}, {"Return", "Enter"}, {"Backspace", L("Backspace", "Retroceso")},
      {"Semicolon", L("; / Ñ", "Ñ / ;")}, {"Quote", "' / ´"}, {"Up", "↑"}, {"Down", "↓"},
      {"Left", "←"}, {"Right", "→"}, {"Control", "Ctrl"},
  };
  std::string base = key, prefix;
  if (key.rfind("Shift+", 0) == 0) base = key.substr(6), prefix = shift;
  for (const auto& [from, to] : names) {
    if (base == from) return prefix + to;
  }
  return prefix + base;
}

// --- Cover art -------------------------------------------------------------

struct Cover {
  SDL_GPUTexture* texture = nullptr;
  SDL_GPUSampler* sampler = nullptr;
  SDL_GPUTextureSamplerBinding binding{};
  int width = 0, height = 0;
  ImTextureID id() const { return (ImTextureID)(intptr_t)&binding; }
};

bool LoadCover(SDL_GPUDevice* gpu, const fs::path& file, Cover& out) {
  SDL_Surface* loaded = SDL_LoadSurface(file.u8string().c_str());
  if (!loaded) return false;
  // R8G8B8A8 in memory order.
  SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ABGR8888);
  SDL_DestroySurface(loaded);
  if (!rgba) return false;
  const Uint32 w = Uint32(rgba->w), h = Uint32(rgba->h);

  SDL_GPUTextureCreateInfo info = {};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  info.width = w;
  info.height = h;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  out.texture = SDL_CreateGPUTexture(gpu, &info);

  SDL_GPUTransferBufferCreateInfo tb_info = {};
  tb_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  tb_info.size = w * h * 4;
  SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(gpu, &tb_info);
  auto* dst = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(gpu, tb, false));
  for (Uint32 y = 0; y < h; ++y) {
    std::memcpy(dst + y * w * 4, static_cast<uint8_t*>(rgba->pixels) + y * rgba->pitch, w * 4);
  }
  SDL_UnmapGPUTransferBuffer(gpu, tb);
  SDL_DestroySurface(rgba);

  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu);
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureTransferInfo src = {};
  src.transfer_buffer = tb;
  SDL_GPUTextureRegion region = {};
  region.texture = out.texture;
  region.w = w;
  region.h = h;
  region.d = 1;
  SDL_UploadToGPUTexture(copy, &src, &region, false);
  SDL_EndGPUCopyPass(copy);
  SDL_SubmitGPUCommandBuffer(cmd);
  SDL_ReleaseGPUTransferBuffer(gpu, tb);

  SDL_GPUSamplerCreateInfo sampler = {};
  sampler.min_filter = SDL_GPU_FILTER_LINEAR;
  sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
  sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
  sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  out.sampler = SDL_CreateGPUSampler(gpu, &sampler);
  out.binding.texture = out.texture;
  out.binding.sampler = out.sampler;
  out.width = int(w);
  out.height = int(h);
  return out.texture && out.sampler;
}

// --- App state -------------------------------------------------------------

struct App {
  SDL_Window* window = nullptr;
  fs::path exe_dir;
  fs::path user_config;
  fs::path launcher_config;  // launcher.toml: the launcher's own UI language
  fs::path manifest_path;
  fs::path game_exe;
  fs::path profiles_root;
  Cover cover;

  Settings settings;
  std::vector<ManifestEntry> manifest;
  Tab tab = Tab::kProfiles;
  std::string notice;

  // Install / verify state
  enum class GameState { kUnknown, kMissing, kIncomplete, kUnverified, kVerified };
  GameState game_state = GameState::kUnknown;
  std::string game_state_detail;
  TaskProgress task;
  std::thread worker;
  std::string task_title;

  // Async file dialogs report here (SDL may call back on another thread).
  std::mutex dialog_mutex;
  std::optional<std::string> picked_iso;
  std::optional<std::string> picked_folder;
  std::optional<std::string> picked_pfp_base;
  std::optional<std::string> picked_pfp_update;

  // What the game folder holds besides the base game (refreshed by CheckGame).
  GameRegion region = GameRegion::kUnknown;
  size_t dlc_packages = 0;
  PfpState pfp;
  // Project Forza Plus 2 choices before installing it.
  std::string pfp_base_zip;
  std::string pfp_update_zip;
  PfpMode pfp_mode = PfpMode::kNormal;
  bool pfp_harder_ai = false;
  // Camera field of view while the mod is installed (current and original).
  bool pfp_fov_ok = false;
  PfpFov pfp_fov;
  PfpFov pfp_fov_original;

  // Profiles
  std::vector<std::string> profiles;
  char new_profile[32] = {};
  std::string delete_candidate;
  bool open_delete_popup = false;

  // Key capture: index into settings.binds, and whether it appends.
  int capture_index = -1;
  bool capture_append = false;

  // Running game
  SDL_Process* game = nullptr;
  fs::file_time_type launched_at;

  // Shader cache: the copy shipped next to the launcher, the runtime's own
  // cache folder, and whether the play button should start the game once the
  // warm-up task finishes.
  fs::path bundled_cache_dir;
  fs::path shader_cache_dir;
  enum class TaskKind { kNone, kImport, kVerify, kWarmup, kMod };
  TaskKind task_kind = TaskKind::kNone;
  std::atomic<int> import_step{0};  // 1 = extracting, 2 = verifying
  bool launch_after_task = false;
  std::string last_exit;

  bool Installed() const {
    return game_state == GameState::kVerified || game_state == GameState::kUnverified;
  }

  fs::path GameDir() const {
    return settings.game_data_root.empty() ? exe_dir / "game" : fs::u8path(settings.game_data_root);
  }

  void Save() {
    settings.game_data_root = GameDir().u8string();
    if (!settings.Save(user_config)) notice = std::string(L("Could not save ", "No se pudo guardar ")) + user_config.u8string();
  }

  void LoadLauncherPrefs() {
    std::ifstream in(launcher_config);
    std::string line;
    while (std::getline(in, line)) {
      if (line.rfind("language", 0) == 0) g_spanish = line.find("\"es\"") != std::string::npos;
    }
  }

  void SaveLauncherPrefs() {
    std::ofstream(launcher_config) << "# Forza2Launcher UI language: \"en\" or \"es\"\nlanguage = \""
                                   << (g_spanish ? "es" : "en") << "\"\n";
  }

  void RefreshProfiles() {
    profiles.clear();
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(profiles_root, ec)) {
      if (e.is_directory() && IsValidGamertag(e.path().filename().u8string())) {
        profiles.push_back(e.path().filename().u8string());
      }
    }
    if (std::find(profiles.begin(), profiles.end(), settings.gamertag) == profiles.end()) {
      profiles.push_back(settings.gamertag);
    }
    std::sort(profiles.begin(), profiles.end());
  }

  void CheckGame() {
    const fs::path dir = GameDir();
    std::error_code ec;
    region = ReadGameRegion(dir / "default.xex");
    dlc_packages = CountDlcPackages(dir);
    pfp = LoadForzaPlus(dir);
    pfp_fov_ok = pfp.installed && ReadForzaPlusFov(dir, false, pfp_fov) &&
                 ReadForzaPlusFov(dir, true, pfp_fov_original);
    if (!fs::exists(dir / "default.xex", ec)) {
      game_state = GameState::kMissing;
      game_state_detail = L("Import your ISO in the Install tab.", "Importa tu ISO en la pestaña Instalación.");
      return;
    }
    TaskProgress quick;
    const VerifyReport report = VerifyGame(dir, WithoutModFiles(manifest, pfp), false, quick);
    if (!report.ok()) {
      game_state = GameState::kIncomplete;
      game_state_detail = std::to_string(report.missing.size()) + L(" files missing, ", " archivos faltan, ") +
                          std::to_string(report.wrong_size.size()) +
                          L(" with a different size. Import the ISO again.",
                            " con otro tamaño. Vuelve a importar la ISO.");
    } else if (fs::exists(VerifiedMarker(dir), ec)) {
      game_state = GameState::kVerified;
      game_state_detail = dir.u8string() + (region == GameRegion::kUsa ? L("\nAmerican release", "\nVersión americana")
                                                                     : L("\nEuropean release", "\nVersión europea"));
    } else {
      game_state = GameState::kUnverified;
      game_state_detail = L("Files present, contents not verified yet.", "Archivos presentes, sin verificar el contenido.");
    }
  }

  bool TaskRunning() const { return task.state == TaskProgress::State::kRunning; }

  void StartTask(std::string title, std::function<bool()> body) {
    if (worker.joinable()) worker.join();
    task_title = std::move(title);
    task_kind = TaskKind::kNone;
    launch_after_task = false;
    task.cancel = false;
    task.done = 0;
    task.total = 0;
    task.SetStatus("");
    task.state = TaskProgress::State::kRunning;
    worker = std::thread([this, body = std::move(body)] {
      const bool ok = body();
      task.state = ok ? TaskProgress::State::kDone
                      : (task.cancel ? TaskProgress::State::kCancelled : TaskProgress::State::kFailed);
    });
  }

  // Full hash check; writes the verified marker on success. Files Project
  // Forza Plus 2 replaced are skipped (their originals are in its backup).
  bool RunVerify(const fs::path& dir) {
    const VerifyReport report = VerifyGame(dir, WithoutModFiles(manifest, LoadForzaPlus(dir)), true, task);
    std::error_code ec;
    if (report.ok() && !task.cancel) {
      std::ofstream(VerifiedMarker(dir)) << "verified against game_files.sha256\n";
      task.SetStatus(std::string(L("All good: ", "Todo correcto: ")) + std::to_string(manifest.size()) +
                     L(" files match the version the port was built from.",
                       " archivos coinciden con la versión del port."));
      return true;
    }
    fs::remove(VerifiedMarker(dir), ec);
    std::string text = std::string(L("Verification failed: ", "La verificación falló: ")) +
                       std::to_string(report.missing.size()) + L(" missing, ", " faltan, ") +
                       std::to_string(report.wrong_size.size()) + L(" wrong size, ", " con otro tamaño, ") +
                       std::to_string(report.wrong_hash.size()) + L(" wrong contents.", " con contenido distinto.");
    const auto& first = !report.missing.empty()      ? report.missing
                        : !report.wrong_size.empty() ? report.wrong_size
                                                     : report.wrong_hash;
    if (!first.empty()) text += std::string(L("\nFirst: ", "\nPrimero: ")) + first.front();
    if (std::find(report.wrong_hash.begin(), report.wrong_hash.end(), "default.xex") !=
        report.wrong_hash.end()) {
      text += L("\ndefault.xex does not match: the port runs the European and American releases, not "
                "other revisions of the game.",
                "\ndefault.xex no coincide: el port funciona con la versión europea y la americana, no con "
                "otras revisiones del juego.");
    }
    task.SetStatus(text);
    return false;
  }

  void LaunchGame() {
    Save();
    const std::string physics = SetForzaPlusPhysicsRate(GameDir(), settings.refresh_rate);
    if (!physics.empty()) notice = physics;
    if (!fs::exists(game_exe)) {
      notice = std::string(L("Not found: ", "No se encuentra ")) + game_exe.u8string();
      return;
    }
    const std::string exe = game_exe.u8string();
    const std::string cwd = exe_dir.u8string();
    const char* args[] = {exe.c_str(), nullptr};
    // A WIN32-subsystem launcher has no console handles, so the default
    // (inherit stdio) fails in DuplicateHandle; the game logs to files anyway.
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char**>(args));
    SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, cwd.c_str());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    game = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!game) {
      notice = std::string(L("Could not start the game: ", "No se pudo lanzar el juego: ")) + SDL_GetError();
    } else {
      last_exit.clear();
      launched_at = fs::file_time_type::clock::now();
      SDL_MinimizeWindow(window);
    }
  }

  // --- Shader cache warm-up ------------------------------------------------
  //
  // The game rebuilds every shader/pipeline in its cache at startup; the first
  // time on a PC (cold driver cache) that is slow, and anything missing from
  // the cache compiles mid-race as a hitch. So the launcher (1) merges an
  // optional cache shipped next to it (shadercache\, none by default: each
  // player's cache fills as they play) into the player's, then (2) runs the game in
  // --shader_warmup mode, which builds everything and quits, while showing a
  // progress bar. Shader translation depends on the internal resolution and
  // MSAA, so a change there means warming up again.

  static constexpr const char* kCacheFiles[] = {"4D5307EA.xsh", "4D5307EA.rtv.d3d12.xpso"};
  static constexpr size_t kCacheHeaderBytes[] = {8, 12};

  fs::path WarmupMarker() const { return shader_cache_dir / "forza2-warmup.txt"; }

  // Identifies the shipped cache by its file sizes (it only ever grows).
  std::string BundleSignature() const {
    std::string sig;
    std::error_code ec;
    for (const char* name : kCacheFiles) {
      sig += std::to_string(fs::file_size(bundled_cache_dir / name, ec)) + ".";
    }
    return sig;
  }

  std::string WarmupFingerprint() const {
    return "bundle=" + BundleSignature() + " scale=" + std::to_string(settings.resolution_scale) +
           " msaa=" + (settings.native_2x_msaa ? "1" : "0");
  }

  std::string ReadMarkerLine(const char* key) const {
    std::ifstream in(WarmupMarker());
    std::string line;
    while (std::getline(in, line)) {
      if (line.rfind(key, 0) == 0) return line.substr(std::strlen(key));
    }
    return {};
  }

  bool NeedsWarmup() const { return ReadMarkerLine("warm=") != WarmupFingerprint(); }

  // Copies the shipped cache in, or appends its records to an existing one
  // (the runtime skips entries it already has). Once per shipped version.
  void MergeBundledCache() {
    std::error_code ec;
    const std::string bundle = BundleSignature();
    if (ReadMarkerLine("merged=") == bundle) return;
    fs::create_directories(shader_cache_dir, ec);
    for (size_t i = 0; i < std::size(kCacheFiles); ++i) {
      const fs::path src = bundled_cache_dir / kCacheFiles[i];
      const fs::path dst = shader_cache_dir / kCacheFiles[i];
      if (!fs::exists(src, ec)) continue;
      if (!fs::exists(dst, ec)) {
        fs::copy_file(src, dst, ec);
        continue;
      }
      std::ifstream in(src, std::ios::binary);
      std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      std::ifstream existing_in(dst, std::ios::binary);
      const std::string existing((std::istreambuf_iterator<char>(existing_in)),
                                 std::istreambuf_iterator<char>());
      const std::string header = existing.substr(0, kCacheHeaderBytes[i]);
      // Different header = different format version: leave the player's file alone.
      if (data.size() <= header.size() || data.compare(0, header.size(), header) != 0) continue;
      // Already contains the shipped records (e.g. the cache was made on this PC).
      if (existing.compare(0, data.size(), data) == 0) continue;
      std::ofstream(dst, std::ios::binary | std::ios::app)
          .write(data.data() + header.size(), std::streamsize(data.size() - header.size()));
    }
    WriteMarker("merged=", bundle);
  }

  void WriteMarker(const char* key, const std::string& value) {
    std::vector<std::string> lines;
    {
      std::ifstream in(WarmupMarker());
      std::string line;
      while (std::getline(in, line)) {
        if (line.rfind(key, 0) != 0) lines.push_back(line);
      }
    }
    lines.push_back(std::string(key) + value);
    std::ofstream out(WarmupMarker(), std::ios::trunc);
    for (const auto& line : lines) out << line << "\n";
  }

  // Runs on the worker thread. Progress goes 0..1000 through the phases the
  // game logs, easing towards the next phase while it waits.
  bool RunWarmup() {
    task.total = 1000;
    task.done = 0;
    task.SetStatus(L("Preparing the shader cache...", "Preparando la caché de shaders..."));
    MergeBundledCache();
    task.done = 80;

    std::error_code ec;
    fs::create_directories(exe_dir / "logs", ec);
    const fs::path log_path = exe_dir / "logs" / "shader-warmup.log";
    fs::remove(log_path, ec);
    const std::string exe = game_exe.u8string(), cwd = exe_dir.u8string();
    const std::string log_arg = "--log_file=" + log_path.u8string();
    const char* args[] = {exe.c_str(), "--shader_warmup=true", "--log_level=info",
                          "--fullscreen=false", log_arg.c_str(), nullptr};
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char**>(args));
    SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, cwd.c_str());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_Process* proc = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!proc) {
      task.SetStatus(std::string(L("Could not start the game: ", "No se pudo lanzar el juego: ")) + SDL_GetError());
      return false;
    }

    // Phases: start -> shaders translated -> pipelines created -> done.
    const uint64_t targets[] = {250, 550, 980, 1000};
    int phase = 0;
    std::string shaders = "?", pipelines = "?";
    auto phase_start = std::chrono::steady_clock::now();
    int exit_code = -1;
    bool exited = false, complete = false;
    while (!exited) {
      exited = SDL_WaitProcess(proc, false, &exit_code);
      std::ifstream in(log_path);
      std::string line;
      int seen = 0;
      while (std::getline(in, line)) {
        if (line.find("Initializing shader storage") != std::string::npos) seen = std::max(seen, 1);
        auto grab = [&](const char* marker, std::string& out) {
          const size_t at = line.find(marker);
          if (at == std::string::npos) return false;
          const size_t num = line.rfind(' ', at - 1);
          out = line.substr(num + 1, at - num - 1);
          return true;
        };
        if (grab(" shaders from the storage", shaders)) seen = std::max(seen, 2);
        if (grab(" graphics pipelines", pipelines)) seen = std::max(seen, 3);
        if (line.find("shader warm-up complete") != std::string::npos) complete = true;
      }
      if (seen > phase) {
        phase = seen;
        phase_start = std::chrono::steady_clock::now();
      }
      const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - phase_start).count();
      const uint64_t from = phase == 0 ? 80 : targets[phase - 1];
      const uint64_t to = targets[std::min(phase, 3)];
      task.done = std::max<uint64_t>(task.done, from + uint64_t((to - from) * (1.0 - std::exp(-t / 4.0))));
      if (phase < 2) {
        task.SetStatus(L("Loading shaders...", "Cargando shaders..."));
      } else {
        task.SetStatus(std::string(L("Compiling pipelines for ", "Compilando pipelines para ")) + shaders +
                       L(" shaders...", " shaders..."));
      }
      if (task.cancel) {
        SDL_KillProcess(proc, true);
        SDL_WaitProcess(proc, true, &exit_code);
        SDL_DestroyProcess(proc);
        task.SetStatus(L("Cancelled.", "Cancelado."));
        return false;
      }
      if (!exited) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    SDL_DestroyProcess(proc);
    if (exit_code != 0 || !complete) {
      task.SetStatus(L("The shader warm-up failed; its log is logs\\shader-warmup.log.",
                       "La precarga de shaders falló; su registro es logs\\shader-warmup.log."));
      return false;
    }
    task.done = 1000;
    WriteMarker("warm=", WarmupFingerprint());
    fs::remove(log_path, ec);
    task.SetStatus(std::string(L("Shader cache ready: ", "Caché de shaders lista: ")) + shaders +
                   L(" shaders, ", " shaders, ") + pipelines + L(" pipelines.", " pipelines."));
    return true;
  }

  void StartWarmup(bool then_launch) {
    Save();
    StartTask(L("Shader cache", "Caché de shaders"), [this] { return RunWarmup(); });
    task_kind = TaskKind::kWarmup;
    launch_after_task = then_launch;
  }

  // Logs exist to explain failures: after a clean exit, remove the ones this
  // session wrote (forza2_NNN.log and its rotations).
  void DeleteSessionLogs() {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(exe_dir / "logs", ec)) {
      if (e.is_regular_file() && e.path().extension() == ".log" &&
          e.last_write_time(ec) >= launched_at - std::chrono::seconds(2)) {
        fs::remove(e.path(), ec);
      }
    }
  }

  void PollGame() {
    if (!game) return;
    int exit_code = 0;
    if (SDL_WaitProcess(game, false, &exit_code)) {
      SDL_DestroyProcess(game);
      game = nullptr;
      SDL_RestoreWindow(window);
      if (exit_code != 0) {
        char hex[16];
        std::snprintf(hex, sizeof(hex), "0x%08X", unsigned(exit_code));
        last_exit = std::string(L("The game closed with an error (", "El juego se cerró con un error (")) +
                    hex + L("). The log is in the logs folder.", "). El registro está en la carpeta logs.");
      } else {
        DeleteSessionLogs();
      }
    }
  }
};

// --- Widgets ---------------------------------------------------------------

void TextDim(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, kDim);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

void TextColored(const ImVec4& color, const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

void Title(const char* text, float size = 1.0f) {
  ImGui::PushFont(g_bold, ImGui::GetStyle().FontSizeBase * size);
  ImGui::TextUnformatted(text);
  ImGui::PopFont();
}

// A rounded panel with a title; rows go inside a two-column table (label on
// the left, control on the right).
bool BeginCard(const char* id, const char* title, const char* subtitle = nullptr) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, kCard);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(12));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20), S(16)));
  ImGui::BeginChild(id, ImVec2(0, 0),
                    ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
  Title(title, 1.15f);
  if (subtitle) TextDim(subtitle);
  ImGui::Dummy(ImVec2(0, S(4)));
  return true;
}

void EndCard() {
  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
  ImGui::Dummy(ImVec2(0, S(10)));
}

bool BeginRows(const char* id) {
  return ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp);
}

// Starts a settings row: label (+ optional hint) left, control column right.
void Row(const char* label, const char* hint = nullptr) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  if (hint) {
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - S(16));
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.88f);
    ImGui::TextUnformatted(hint);
    ImGui::PopFont();
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
  }
  ImGui::TableSetColumnIndex(1);
  ImGui::SetNextItemWidth(-FLT_MIN);
}

void EndRows() { ImGui::EndTable(); }

// iOS-style switch.
bool Toggle(const char* id, bool* value) {
  const float h = ImGui::GetFrameHeight() * 0.9f;
  const float w = h * 1.85f;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - w));
  ImVec2 p = ImGui::GetCursorScreenPos();
  p.y += (ImGui::GetFrameHeight() - h) * 0.5f;
  ImGui::InvisibleButton(id, ImVec2(w, ImGui::GetFrameHeight()));
  const bool clicked = ImGui::IsItemClicked();
  if (clicked) *value = !*value;
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec4 track = *value ? (hovered ? kAccentHover : kAccent)
                              : (hovered ? ImVec4(0.26f, 0.27f, 0.30f, 1) : ImVec4(0.21f, 0.22f, 0.25f, 1));
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Col(track), h * 0.5f);
  const float r = h * 0.5f - S(3);
  const float cx = *value ? p.x + w - h * 0.5f : p.x + h * 0.5f;
  dl->AddCircleFilled(ImVec2(cx, p.y + h * 0.5f), r, IM_COL32(255, 255, 255, 255));
  return clicked;
}

// Row of buttons, one selected. Fills the available width.
template <typename T>
bool Segmented(const char* id, T& value, const std::vector<std::pair<T, const char*>>& items) {
  ImGui::PushID(id);
  bool changed = false;
  const float full = ImGui::GetContentRegionAvail().x;
  const float gap = S(4);
  const float each = (full - gap * float(items.size() - 1)) / float(items.size());
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(7));
  for (size_t i = 0; i < items.size(); ++i) {
    const bool active = items[i].first == value;
    ImGui::PushStyleColor(ImGuiCol_Button, active ? kAccent : kControl);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? kAccentHover : ImVec4(0.22f, 0.23f, 0.26f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccent);
    if (i) ImGui::SameLine();
    ImGui::PushID(int(i));
    if (ImGui::Button(items[i].second, ImVec2(each, 0)) && !active) {
      value = items[i].first;
      changed = true;
    }
    ImGui::PopID();
    ImGui::PopStyleColor(3);
  }
  ImGui::PopStyleVar(2);
  ImGui::PopID();
  return changed;
}

template <typename T>
bool Combo(const char* id, T& value, const std::vector<std::pair<T, const char*>>& items) {
  const char* preview = "";
  for (const auto& [v, name] : items) {
    if (v == value) preview = name;
  }
  bool changed = false;
  if (ImGui::BeginCombo(id, preview)) {
    for (const auto& [v, name] : items) {
      if (ImGui::Selectable(name, v == value)) {
        value = v;
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

bool PrimaryButton(const char* label, const ImVec2& size) {
  ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccent);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
  const bool pressed = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
  return pressed;
}

// Small rounded label, e.g. a key or "Activo".
void Chip(const char* text, const ImVec4& bg, const ImVec4& fg = kText) {
  const ImVec2 pad(S(8), S(3));
  const ImVec2 size = ImGui::CalcTextSize(text);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float y = p.y + (ImGui::GetFrameHeight() - size.y - pad.y * 2) * 0.5f;
  ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, y),
                                            ImVec2(p.x + size.x + pad.x * 2, y + size.y + pad.y * 2),
                                            Col(bg), S(6));
  ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + pad.x, y + pad.y), Col(fg), text);
  ImGui::Dummy(ImVec2(size.x + pad.x * 2, ImGui::GetFrameHeight()));
}

void StatusDot(const ImVec4& color) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float r = S(5);
  ImGui::GetWindowDrawList()->AddCircleFilled(
      ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f + S(1)), r, Col(color));
  ImGui::Dummy(ImVec2(r * 2 + S(6), ImGui::GetTextLineHeight()));
  ImGui::SameLine(0, 0);
}

// --- Left column: cover, profile, play ------------------------------------

// Fixed window layout: the left column is exactly as wide as the cover, the
// cover sits whole at the top, the profile/play controls below it.
float CoverWidth() { return S(400); }
float CoverHeight(const App& app) {
  const float aspect = app.cover.texture ? float(app.cover.height) / float(app.cover.width) : 1.5f;
  return CoverWidth() * aspect;
}
float ControlsHeight() { return S(250); }
float SettingsWidth() { return S(800); }

void CoverPanel(App& app, const ImVec2& size) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 p1(p0.x + size.x, p0.y + CoverHeight(app));

  if (app.cover.texture) {
    dl->AddImage(app.cover.id(), p0, p1);
  } else {
    // No cover.png: a panel in the box-art colours.
    dl->AddRectFilledMultiColor(p0, p1, IM_COL32(205, 72, 22, 255), IM_COL32(150, 30, 20, 255),
                                IM_COL32(40, 14, 12, 255), IM_COL32(70, 20, 14, 255));
    ImGui::PushFont(g_bold, ImGui::GetStyle().FontSizeBase * 3.2f);
    dl->AddText(ImVec2(p0.x + S(28), p0.y + S(40)), IM_COL32(255, 255, 255, 240), "FORZA 2");
    ImGui::PopFont();
    ImGui::PushFont(g_bold, ImGui::GetStyle().FontSizeBase * 1.2f);
    dl->AddText(ImVec2(p0.x + S(32), p0.y + S(110)), IM_COL32(255, 210, 180, 230), "MOTORSPORT");
    ImGui::PopFont();
  }

  // Controls under the cover.
  const float pad = S(22);
  const float inner_w = size.x - pad * 2;
  ImGui::SetCursorScreenPos(ImVec2(p0.x + pad, p1.y + S(18)));
  ImGui::BeginGroup();
  ImGui::PushStyleColor(ImGuiCol_Text, kDim);
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.85f);
  ImGui::TextUnformatted(L("PROFILE", "PERFIL"));
  ImGui::PopFont();
  ImGui::PopStyleColor();
  ImGui::SetNextItemWidth(inner_w);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, kControl);
  if (ImGui::BeginCombo("##profile", app.settings.gamertag.c_str())) {
    for (const auto& name : app.profiles) {
      if (ImGui::Selectable(name.c_str(), name == app.settings.gamertag)) {
        app.settings.gamertag = name;
        app.Save();
      }
    }
    ImGui::Separator();
    if (ImGui::Selectable(L("Manage profiles...", "Gestionar perfiles..."))) app.tab = Tab::kProfiles;
    ImGui::EndCombo();
  }
  ImGui::PopStyleColor();
  ImGui::Dummy(ImVec2(0, S(6)));

  switch (app.game_state) {
    case App::GameState::kVerified: StatusDot(kOk); ImGui::TextUnformatted(L("Game installed and verified", "Juego instalado y verificado")); break;
    case App::GameState::kUnverified: StatusDot(kWarn); ImGui::TextUnformatted(L("Game installed (not verified)", "Juego instalado (sin verificar)")); break;
    case App::GameState::kIncomplete: StatusDot(kBad); ImGui::TextUnformatted(L("Incomplete install", "Instalación incompleta")); break;
    case App::GameState::kMissing: StatusDot(kBad); ImGui::TextUnformatted(L("Game not installed", "Juego no instalado")); break;
    default: break;
  }
  ImGui::Dummy(ImVec2(0, S(6)));

  if (app.TaskRunning()) {
    // Any running task (import, verification, shader warm-up) takes the play
    // button's place, so its progress is always on screen.
    const float fraction = app.task.total ? float(double(app.task.done) / double(app.task.total)) : 0.0f;
    const char* heading = app.task_kind == App::TaskKind::kImport   ? L("IMPORTING GAME", "IMPORTANDO JUEGO")
                          : app.task_kind == App::TaskKind::kVerify ? L("VERIFYING FILES", "VERIFICANDO ARCHIVOS")
                          : app.task_kind == App::TaskKind::kMod    ? "PROJECT FORZA PLUS 2"
                                                                    : L("PREPARING SHADERS", "PREPARANDO SHADERS");
    ImGui::PushFont(g_bold, ImGui::GetStyle().FontSizeBase * 1.05f);
    ImGui::Text("%s  %d%%", heading, int(fraction * 100));
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kAccent);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(6));
    ImGui::ProgressBar(fraction, ImVec2(inner_w, S(14)), "");
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (app.task_kind == App::TaskKind::kImport) {
      ImGui::TextUnformatted(app.import_step == 2 ? L("Step 2/2: verifying", "Paso 2/2: verificando")
                                                  : L("Step 1/2: extracting", "Paso 1/2: extrayendo"));
      ImGui::SameLine();
    }
    if (app.task_kind != App::TaskKind::kWarmup && app.task.total) {
      ImGui::Text("%.2f / %.2f GB", double(app.task.done) / 1e9, double(app.task.total) / 1e9);
    }
    if (ImGui::SmallButton(L("Cancel", "Cancelar"))) app.task.cancel = true;
    ImGui::PushTextWrapPos(p0.x + size.x - pad);
    TextDim(app.task.Status().c_str());
    ImGui::PopTextWrapPos();
  } else {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(10));
    ImGui::PushFont(g_bold, ImGui::GetStyle().FontSizeBase * 1.45f);
    if (app.Installed()) {
      ImGui::BeginDisabled(app.game || app.TaskRunning());
      if (PrimaryButton(app.game ? L("PLAYING", "EN JUEGO") : L("PLAY", "JUGAR"), ImVec2(inner_w, S(62)))) {
        // First run (or new cache / graphics change): warm the shader cache
        // first; the game starts by itself when that finishes.
        if (app.NeedsWarmup()) {
          app.StartWarmup(true);
        } else {
          app.LaunchGame();
        }
      }
      ImGui::EndDisabled();
    } else if (PrimaryButton(L("INSTALL", "INSTALAR"), ImVec2(inner_w, S(62)))) {
      app.tab = Tab::kInstall;
    }
    ImGui::PopFont();
    ImGui::PopStyleVar();
  }
  if (app.task.state == TaskProgress::State::kFailed) {
    ImGui::PushTextWrapPos(p0.x + size.x - pad);
    TextColored(kBad, app.task.Status().c_str());
    ImGui::PopTextWrapPos();
  }
  if (!app.last_exit.empty()) {
    ImGui::PushTextWrapPos(p0.x + size.x - pad);
    TextColored(kBad, app.last_exit.c_str());
    ImGui::PopTextWrapPos();
  }
  ImGui::EndGroup();
  ImGui::SetCursorScreenPos(p0);
  ImGui::Dummy(size);
}

// --- Tabs ------------------------------------------------------------------

void TabProfiles(App& app) {
  BeginCard("##profiles", L("Profiles", "Perfiles"),
            L("Each profile is a gamertag with its own saves, cars and options.",
              "Cada perfil es un gamertag con sus propias partidas, coches y opciones."));
  const float tile_w = S(180), tile_h = S(150);
  const float avail = ImGui::GetContentRegionAvail().x;
  const int per_row = std::max(1, int((avail + S(12)) / (tile_w + S(12))));
  int index = 0;
  for (const auto& name : app.profiles) {
    const bool active = name == app.settings.gamertag;
    if (index % per_row) ImGui::SameLine(0, S(12));
    ImGui::PushID(name.c_str());
    ImGui::BeginGroup();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("tile", ImVec2(tile_w, tile_h));
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked() && !active) {
      app.settings.gamertag = name;
      app.Save();
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + tile_w, p.y + tile_h),
                      Col(hovered ? ImVec4(0.17f, 0.18f, 0.20f, 1) : kControl), S(12));
    if (active) dl->AddRect(p, ImVec2(p.x + tile_w, p.y + tile_h), Col(kAccent), S(12), 0, S(2));
    // Avatar: initial in a circle.
    const ImVec2 c(p.x + tile_w * 0.5f, p.y + S(52));
    dl->AddCircleFilled(c, S(30), Col(active ? kAccent : ImVec4(0.30f, 0.31f, 0.35f, 1)));
    const char initial[2] = {char(std::toupper(static_cast<unsigned char>(name[0]))), 0};
    ImGui::PushFont(g_bold, ImGui::GetStyle().FontSizeBase * 1.6f);
    const ImVec2 isz = ImGui::CalcTextSize(initial);
    dl->AddText(ImVec2(c.x - isz.x * 0.5f, c.y - isz.y * 0.5f), IM_COL32_WHITE, initial);
    ImGui::PopFont();
    const ImVec2 nsz = ImGui::CalcTextSize(name.c_str());
    dl->AddText(ImVec2(p.x + (tile_w - nsz.x) * 0.5f, p.y + S(94)), Col(kText), name.c_str());
    const char* tag = active ? L("Active", "Activo") : L("Use", "Usar");
    const ImVec2 tsz = ImGui::CalcTextSize(tag);
    dl->AddText(ImVec2(p.x + (tile_w - tsz.x) * 0.5f, p.y + S(120)), Col(active ? kAccent : kDim), tag);
    // Below the tile, outside it, so the click never reaches the tile: delete
    // for every profile except the active one (which keeps the space empty so
    // the rows stay aligned).
    if (!active) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(kBad.x, kBad.y, kBad.z, 0.25f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(kBad.x, kBad.y, kBad.z, 0.45f));
      ImGui::PushStyleColor(ImGuiCol_Text, kBad);
      if (ImGui::Button(L("Delete profile", "Eliminar perfil"), ImVec2(tile_w, 0))) {
        app.delete_candidate = name;
        app.open_delete_popup = true;
      }
      ImGui::PopStyleColor(4);
    } else {
      ImGui::Dummy(ImVec2(tile_w, ImGui::GetFrameHeight()));
    }
    ImGui::EndGroup();
    ImGui::PopID();
    ++index;
  }
  EndCard();

  BeginCard("##newprofile", L("New profile", "Nuevo perfil"),
            L("1 to 15 letters, digits or spaces.", "De 1 a 15 letras, números o espacios."));
  const float button_w = S(120);
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - button_w - S(10));
  const bool enter = ImGui::InputTextWithHint("##new", L("Profile name", "Nombre del perfil"), app.new_profile,
                                              sizeof(app.new_profile),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
  const std::string name = app.new_profile;
  const bool valid = IsValidGamertag(name);
  const bool exists = std::find(app.profiles.begin(), app.profiles.end(), name) != app.profiles.end();
  ImGui::SameLine(0, S(10));
  ImGui::BeginDisabled(!valid || exists);
  if (PrimaryButton(L("Create", "Crear"), ImVec2(button_w, 0)) || (enter && valid && !exists)) {
    std::error_code ec;
    fs::create_directories(app.profiles_root / fs::u8path(name), ec);
    app.settings.gamertag = name;
    app.new_profile[0] = 0;
    app.Save();
    app.RefreshProfiles();
  }
  ImGui::EndDisabled();
  if (!name.empty() && !valid) TextColored(kWarn, L("Only letters, digits and single spaces.", "Solo letras, números y espacios simples."));
  else if (exists) TextColored(kWarn, L("A profile with that name already exists.", "Ya existe un perfil con ese nombre."));
  EndCard();

  if (app.open_delete_popup) {
    ImGui::OpenPopup("###delete_profile");
    app.open_delete_popup = false;
  }
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  const std::string popup_title = std::string(L("Delete profile", "Eliminar perfil")) + "###delete_profile";
  if (ImGui::BeginPopupModal(popup_title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text(L("Delete profile \"%s\" and its saves?", "¿Eliminar el perfil \"%s\" y sus partidas?"),
                app.delete_candidate.c_str());
    TextDim(L("It is moved to Documents\\forza2\\profiles-papelera, in case you change your mind.",
              "Se mueve a Documentos\\forza2\\profiles-papelera, por si te arrepientes."));
    ImGui::Dummy(ImVec2(0, S(6)));
    ImGui::PushStyleColor(ImGuiCol_Button, kBad);
    if (ImGui::Button(L("Delete", "Eliminar"), ImVec2(S(130), 0))) {
      const auto stamp = std::to_string(std::time(nullptr));
      const fs::path trash = app.profiles_root.parent_path() / "profiles-papelera";
      std::error_code ec;
      fs::create_directories(trash, ec);
      const fs::path from = app.profiles_root / fs::u8path(app.delete_candidate);
      if (fs::exists(from, ec)) fs::rename(from, trash / fs::u8path(app.delete_candidate + "-" + stamp), ec);
      if (ec) app.notice = std::string(L("Could not delete the profile: ", "No se pudo eliminar el perfil: ")) + ec.message();
      app.RefreshProfiles();
      ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button(L("Cancel", "Cancelar"), ImVec2(S(130), 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

// Quality presets map onto the individual settings; anything else is "custom".
struct Preset {
  int id;
  const char* name;
  int scale, aniso;
  bool msaa;
  const char* aa;
  const char* present;
};
const Preset kPresets[] = {
    {0, "Original 720p", 1, 3, true, "none", "bilinear"},
    {1, "High 1440p", 2, 5, true, "none", "bilinear"},
    {2, "Ultra 4K", 3, 5, true, "fxaa", "cas"},
};

int CurrentPreset(const Settings& s) {
  for (const auto& p : kPresets) {
    if (s.resolution_scale == p.scale && s.anisotropic_override == p.aniso &&
        s.native_2x_msaa == p.msaa && s.swap_post_effect == p.aa && s.present_effect == p.present) {
      return p.id;
    }
  }
  return 3;
}

void TabGraphics(App& app) {
  Settings& s = app.settings;
  bool changed = false;

  BeginCard("##display", L("Display", "Pantalla"));
  if (BeginRows("##d")) {
    Row(L("Mode", "Modo"));
    int mode = s.fullscreen ? 1 : 0;
    if (Segmented<int>("mode", mode, {{1, L("Fullscreen", "Pantalla completa")}, {0, L("Window", "Ventana")}})) {
      s.fullscreen = mode == 1;
      changed = true;
    }
    Row(L("Monitor", "Monitor"));
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    std::vector<std::string> names;
    for (int i = 0; i < count; ++i) {
      const char* n = SDL_GetDisplayName(displays[i]);
      names.push_back(std::to_string(i + 1) + " · " + (n ? n : "Monitor"));
    }
    SDL_free(displays);
    std::vector<std::pair<int, const char*>> monitors{{0, L("Primary", "Principal")}};
    for (int i = 0; i < count; ++i) monitors.emplace_back(i + 1, names[i].c_str());
    changed |= Combo<int>("##monitor", s.monitor, monitors);
    if (!s.fullscreen) {
      Row(L("Window size", "Tamaño de ventana"));
      int size = s.window_width * 10000 + s.window_height;
      if (Combo<int>("##winsize", size,
                     {{12800720, "1280 × 720"},
                      {16000900, "1600 × 900"},
                      {19201080, "1920 × 1080"},
                      {25601440, "2560 × 1440"},
                      {38402160, "3840 × 2160"}})) {
        s.window_width = size / 10000;
        s.window_height = size % 10000;
        changed = true;
      }
    }
    Row(L("Refresh rate", "Frecuencia"));
    changed |= Segmented<int>("refresh", s.refresh_rate, {{60, "60 Hz"}, {120, "120 Hz"}});
    Row(L("Keep 16:9", "Mantener 16:9"));
    changed |= Toggle("##letterbox", &s.present_letterbox);
    EndRows();
  }
  EndCard();

  BeginCard("##quality", L("Image quality", "Calidad de imagen"));
  int preset = CurrentPreset(s);
  if (Segmented<int>("preset", preset,
                     {{0, "Original 720p"}, {1, L("High 1440p", "Alta 1440p")}, {2, "Ultra 4K"},
                      {3, L("Custom", "Personalizada")}}) &&
      preset < 3) {
    const Preset& p = kPresets[preset];
    s.resolution_scale = p.scale;
    s.anisotropic_override = p.aniso;
    s.native_2x_msaa = p.msaa;
    s.swap_post_effect = p.aa;
    s.present_effect = p.present;
    changed = true;
  }
  ImGui::Dummy(ImVec2(0, S(8)));
  if (BeginRows("##q")) {
    Row(L("Internal resolution", "Resolución interna"));
    changed |= Segmented<int>("scale", s.resolution_scale,
                              {{1, "720p (1×)"}, {2, "1440p (2×)"}, {3, "4K (3×)"}});
    Row(L("Anisotropic filtering", "Filtrado anisótropo"));
    changed |= Combo<int>("##aniso", s.anisotropic_override,
                          {{-1, L("Game default", "Lo que pida el juego")},
                           {0, L("Off", "Desactivado")},
                           {2, "2×"},
                           {3, "4×"},
                           {4, "8×"},
                           {5, "16×"}});
    Row(L("Native 2× MSAA", "MSAA 2× nativo"));
    changed |= Toggle("##msaa", &s.native_2x_msaa);
    Row(L("Extra anti-aliasing", "Antialiasing extra"));
    changed |= Segmented<std::string>(
        "aa", s.swap_post_effect, {{"none", L("None", "Ninguno")}, {"fxaa", "FXAA"}, {"fxaa_extreme", "FXAA+"}});
    Row(L("Output scaling", "Escalado a pantalla"));
    changed |= Segmented<std::string>(
        "present", s.present_effect, {{"bilinear", L("Bilinear", "Bilineal")}, {"cas", "AMD CAS"}, {"fsr", "AMD FSR"}});
    EndRows();
  }
  EndCard();

  if (ImGui::Button(L("Restore defaults", "Restaurar valores predeterminados"))) {
    s.ResetDisplay();
    changed = true;
  }
  if (changed) app.Save();
}

void TabControls(App& app) {
  Settings& s = app.settings;
  bool changed = false;

  BeginCard("##pad", L("Controller", "Mando"));
  if (BeginRows("##p")) {
    Row(L("Input system", "Sistema de entrada"));
    changed |= Segmented<std::string>("backend", s.input_backend, {{"sdl", "SDL"}, {"xinput", "XInput"}});
    EndRows();
  }
  EndCard();

  BeginCard("##kb", L("Keyboard and mouse", "Teclado y ratón"));
  if (BeginRows("##k")) {
    Row(L("Use the keyboard as a controller", "Usar el teclado como mando"));
    changed |= Toggle("##mnk", &s.mnk_mode);
    if (s.mnk_mode) {
      Row(L("Mouse as right stick", "Ratón como stick derecho"));
      changed |= Toggle("##mouse", &s.mnk_mouse);
      if (s.mnk_mouse) {
        Row(L("Mouse sensitivity", "Sensibilidad del ratón"));
        float sens = float(s.mnk_sensitivity);
        if (ImGui::SliderFloat("##sens", &sens, 0.1f, 5.0f, "%.1f")) {
          s.mnk_sensitivity = sens;
          changed = true;
        }
      }
    }
    EndRows();
  }
  EndCard();

  if (s.mnk_mode) {
    BeginCard("##binds", L("Key bindings", "Asignación de teclas"));
    if (app.capture_index >= 0) {
      TextColored(kAccent, L("Press a key to assign it  ·  Esc cancels", "Pulsa una tecla para asignarla  ·  Esc cancela"));
    }
    const char* group = "";
    for (int i = 0; i < int(s.binds.size()); ++i) {
      auto& b = s.binds[i];
      if (std::strcmp(group, b.group) != 0) {
        group = b.group;
        const char* group_label = L(b.group, b.group_es);
        ImGui::Dummy(ImVec2(0, S(6)));
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
        ImGui::PushFont(g_bold, ImGui::GetStyle().FontSizeBase * 0.9f);
        ImGui::TextUnformatted(group_label);
        ImGui::PopFont();
        ImGui::PopStyleColor();
      }
      ImGui::PushID(i);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(L(b.label, b.label_es));
      ImGui::SameLine(S(230));
      if (app.capture_index == i) {
        Chip("...", kAccent);
      } else if (b.keys.empty()) {
        Chip(L("unassigned", "sin asignar"), kControl, kDim);
      } else {
        size_t start = 0;
        bool first = true;
        while (start <= b.keys.size()) {
          const size_t comma = b.keys.find(',', start);
          const std::string key = b.keys.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
          if (!first) ImGui::SameLine(0, S(6));
          Chip(KeyLabel(key).c_str(), kControl);
          first = false;
          if (comma == std::string::npos) break;
          start = comma + 1;
        }
      }
      const float buttons_w = S(250);
      ImGui::SameLine(ImGui::GetContentRegionMax().x - buttons_w);
      if (ImGui::Button(L("Change", "Cambiar"), ImVec2(S(80), 0))) app.capture_index = i, app.capture_append = false;
      ImGui::SameLine(0, S(4));
      if (ImGui::Button("+", ImVec2(S(32), 0))) app.capture_index = i, app.capture_append = true;
      ImGui::SameLine(0, S(4));
      if (ImGui::Button(L("Clear", "Quitar"), ImVec2(S(64), 0))) b.keys.clear(), changed = true;
      ImGui::SameLine(0, S(4));
      if (ImGui::Button("↺", ImVec2(S(32), 0))) b.keys = b.default_keys, changed = true;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip(L("Default key: %s", "Tecla por defecto: %s"), b.default_keys);
      ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, S(8)));
    if (ImGui::Button(L("Restore all keys", "Restaurar todas las teclas"))) {
      s.ResetBinds();
      changed = true;
    }
    EndCard();
  }
  if (changed) app.Save();
}

void TabAudio(App& app) {
  Settings& s = app.settings;
  bool changed = false;
  BeginCard("##lang", L("Language", "Idioma"));
  if (BeginRows("##l")) {
    Row(L("Game language", "Idioma del juego"));
    changed |= Combo<int>("##language", s.user_language,
                          {{5, "Español"}, {1, "English"}, {4, "Français"}, {3, "Deutsch"}, {6, "Italiano"}});
    EndRows();
  }
  EndCard();
  BeginCard("##sound", L("Sound", "Sonido"));
  if (BeginRows("##s")) {
    Row(L("Mute the game", "Silenciar el juego"));
    changed |= Toggle("##mute", &s.audio_mute);
    EndRows();
  }
  EndCard();
  if (changed) app.Save();
}

// Opens a .zip picker; the result lands in app.*slot (guarded by dialog_mutex).
void PickZip(App& app, std::optional<std::string> App::*slot) {
  static const SDL_DialogFileFilter filters[] = {{"Zip", "zip"}};
  struct Request {
    App* app;
    std::optional<std::string> App::*slot;
  };
  SDL_ShowOpenFileDialog(
      [](void* user, const char* const* files, int) {
        std::unique_ptr<Request> request(static_cast<Request*>(user));
        if (files && files[0]) {
          std::lock_guard lock(request->app->dialog_mutex);
          request->app->*(request->slot) = files[0];
        }
      },
      new Request{&app, slot}, app.window, filters, 1, nullptr, false);
}

void CardTask(App& app);

void CardForzaPlusMod(App& app) {
  BeginCard("##pfp", "Project Forza Plus 2",
            L("Community mod for the American release: reworked forced induction, tyre physics and PI, the "
              "DLC cars and tracks, and four career modes. Installed from its .zip; the original files are "
              "kept, so it can be removed.",
              "Mod de la comunidad para la versión americana: sobrealimentación, física de neumáticos y PI "
              "rehechos, los coches y circuitos DLC y cuatro modos de trayectoria. Se instala desde su .zip "
              "y guarda los archivos originales, así que se puede quitar."));
  const std::vector<std::pair<PfpMode, const char*>> modes = {{PfpMode::kNormal, "Normal"},
                                                              {PfpMode::kHard, L("Hard", "Difícil")},
                                                              {PfpMode::kSandbox, "Sandbox"},
                                                              {PfpMode::kVanilla, "Vanilla"}};
  const auto file_button = [](const std::string& path, const char* id) {
    const std::string label =
        (path.empty() ? std::string(L("Choose .zip", "Elegir .zip")) : fs::u8path(path).filename().u8string()) + id;
    return ImGui::Button(label.c_str());
  };
  if (!app.Installed()) {
    StatusDot(kWarn);
    ImGui::TextUnformatted(L("Install the game first (Install tab).", "Instala antes el juego (pestaña Instalación)."));
  } else if (app.region != GameRegion::kUsa) {
    StatusDot(kWarn);
    ImGui::TextUnformatted(L("Needs the American release of the game; the installed one is European.",
                             "Necesita la versión americana del juego; la instalada es la europea."));
  } else if (!app.pfp.installed) {
    // Step 1: the mod itself.
    ImGui::BeginDisabled(app.TaskRunning() || app.game);
    if (BeginRows("##pfp_new")) {
      Row(L("Mod archive", "Archivo del mod"), "Project Forza Plus 2 v1.3.zip");
      if (file_button(app.pfp_base_zip, "##pfp_base")) PickZip(app, &App::picked_pfp_base);
      Row(L("Career mode", "Modo de trayectoria"));
      Segmented<PfpMode>("pfp_mode", app.pfp_mode, modes);
      Row(L("Harder AI", "IA más difícil"));
      Toggle("##pfp_ai", &app.pfp_harder_ai);
      EndRows();
    }
    ImGui::BeginDisabled(app.pfp_base_zip.empty());
    if (PrimaryButton(L("Install Project Forza Plus 2", "Instalar Project Forza Plus 2"), ImVec2(S(300), S(40)))) {
      const fs::path game_dir = app.GameDir();
      const fs::path base = fs::u8path(app.pfp_base_zip);
      const PfpMode mode = app.pfp_mode;
      const bool harder_ai = app.pfp_harder_ai;
      app.StartTask("Project Forza Plus 2", [&app, game_dir, base, mode, harder_ai] {
        return InstallForzaPlus(game_dir, base, fs::path(), mode, harder_ai, app.task);
      });
      app.task_kind = App::TaskKind::kMod;
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
  } else {
    StatusDot(kOk);
    ImGui::TextUnformatted(app.pfp.updated ? L("Installed, with the update", "Instalado, con la actualización")
                                           : L("Installed", "Instalado"));
    ImGui::BeginDisabled(app.TaskRunning() || app.game);
    PfpMode mode = app.pfp.mode;
    bool harder_ai = app.pfp.harder_ai;
    bool changed = false;
    if (BeginRows("##pfp_set")) {
      Row(L("Career mode", "Modo de trayectoria"),
          L("The mod suggests a separate profile for each mode.",
            "El mod recomienda un perfil distinto para cada modo."));
      changed |= Segmented<PfpMode>("pfp_mode", mode, modes);
      Row(L("Harder AI", "IA más difícil"));
      changed |= Toggle("##pfp_ai", &harder_ai);
      // Step 2: the update, once the mod is in.
      if (!app.pfp.updated) {
        Row(L("Update", "Actualización"), "Project Forza Plus 2 Update v1.3.zip");
        if (file_button(app.pfp_update_zip, "##pfp_update")) PickZip(app, &App::picked_pfp_update);
        if (!app.pfp_update_zip.empty()) {
          ImGui::SameLine();
          if (ImGui::SmallButton(L("Apply##pfp_update", "Aplicar##pfp_update"))) {
            const fs::path game_dir = app.GameDir();
            const fs::path update = fs::u8path(app.pfp_update_zip);
            app.StartTask("Project Forza Plus 2", [&app, game_dir, update] {
              return UpdateForzaPlus(game_dir, update, app.task);
            });
            app.task_kind = App::TaskKind::kMod;
          }
        }
      }
      EndRows();
    }
    if (changed) {
      const std::string error = SetForzaPlusMode(app.GameDir(), mode, harder_ai);
      if (!error.empty()) app.notice = error;
      app.CheckGame();
    }
    if (ImGui::Button(L("Uninstall", "Desinstalar"), ImVec2(S(170), 0))) {
      const fs::path game_dir = app.GameDir();
      app.StartTask("Project Forza Plus 2", [&app, game_dir] { return UninstallForzaPlus(game_dir, app.task); });
      app.task_kind = App::TaskKind::kMod;
    }
    ImGui::EndDisabled();
  }
  EndCard();
}

void CardForzaPlusFov(App& app) {
  BeginCard("##pfp_fov", L("Field of view", "Campo de visión"),
            L("Camera angle in degrees for each view: under 100 mph, 100-150 mph and over 150 mph. Takes "
              "effect the next time the game starts.",
              "Ángulo de la cámara en grados para cada vista: a menos de 160 km/h, entre 160 y 240 km/h y "
              "a más de 240 km/h. Se aplica la próxima vez que arranques el juego."));
  if (!app.pfp.installed) {
    TextDim(L("Install the mod first: the game only accepts a changed camera file with the mod's executable.",
              "Instala antes el mod: el juego solo acepta el archivo de cámaras cambiado con el ejecutable del mod."));
    EndCard();
    return;
  }
  if (!app.pfp_fov_ok) {
    TextDim(L("Media\\Camera\\CameraSettings.ini is missing: verify the game files.",
              "Falta Media\\Camera\\CameraSettings.ini: verifica los archivos del juego."));
    EndCard();
    return;
  }
  const char* names[kCameraCount] = {L("Chase, near", "Persecución cercana"), L("Chase, far", "Persecución lejana"),
                                     L("Hood", "Capó"), L("Bumper", "Parachoques")};
  bool commit = false;
  ImGui::BeginDisabled(app.TaskRunning() || app.game);
  if (BeginRows("##pfp_fov_rows")) {
    for (int c = 0; c < kCameraCount; ++c) {
      const int* o = app.pfp_fov_original.degrees[c];
      char hint[64];
      std::snprintf(hint, sizeof(hint), "%s %d / %d / %d", L("Original:", "Original:"), o[0], o[1], o[2]);
      Row(names[c], hint);
      ImGui::PushID(c);
      ImGui::SliderInt3("##fov", app.pfp_fov.degrees[c], 30, 110);
      commit |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::PopID();
    }
    EndRows();
  }
  if (ImGui::Button(L("Original values", "Valores originales"), ImVec2(S(200), 0))) {
    app.pfp_fov = app.pfp_fov_original;
    commit = true;
  }
  ImGui::EndDisabled();
  if (commit) {
    const std::string error = SetForzaPlusFov(app.GameDir(), app.pfp_fov);
    if (!error.empty()) app.notice = error;
    app.CheckGame();
  }
  EndCard();
}

void TabForzaPlus(App& app) {
  CardTask(app);
  CardForzaPlusMod(app);
  CardForzaPlusFov(app);
}

// Progress / result of the running or last task (import, verification, mod).
void CardTask(App& app) {
  const auto state = app.task.state.load();
  if (state == TaskProgress::State::kIdle) return;
  BeginCard("##task", app.task_title.c_str());
  if (state == TaskProgress::State::kRunning) {
    const uint64_t total = app.task.total, done = app.task.done;
    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%.1f / %.1f GB", double(done) / 1e9, double(total) / 1e9);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kAccent);
    ImGui::ProgressBar(total ? float(double(done) / double(total)) : 0.0f, ImVec2(-1, S(10)), "");
    ImGui::PopStyleColor();
    ImGui::Text("%s", total ? overlay : L("Preparing...", "Preparando..."));
    TextDim(app.task.Status().c_str());
    if (ImGui::Button(L("Cancel", "Cancelar"))) app.task.cancel = true;
  } else {
    TextColored(state == TaskProgress::State::kDone        ? kOk
                : state == TaskProgress::State::kCancelled ? kWarn
                                                           : kBad,
                app.task.Status().c_str());
  }
  EndCard();
}

// Opens the .iso picker; the import task starts from the main loop.
void PickIso(App& app) {
  // SDL reads the filters until the callback runs, so they must outlive this frame.
  static const SDL_DialogFileFilter filters_en[] = {{"Xbox 360 disc image", "iso"}, {"All files", "*"}};
  static const SDL_DialogFileFilter filters_es[] = {{"Imagen de disco de Xbox 360", "iso"},
                                                    {"Todos los archivos", "*"}};
  const SDL_DialogFileFilter* filters = g_spanish ? filters_es : filters_en;
  SDL_ShowOpenFileDialog(
      [](void* user, const char* const* files, int) {
        auto* a = static_cast<App*>(user);
        if (files && files[0]) {
          std::lock_guard lock(a->dialog_mutex);
          a->picked_iso = files[0];
        }
      },
      &app, app.window, filters, 2, nullptr, false);
}

void TabInstall(App& app) {
  BeginCard("##state", L("Status", "Estado"));
  switch (app.game_state) {
    case App::GameState::kVerified: StatusDot(kOk); Title(L("Installed and verified", "Instalado y verificado")); break;
    case App::GameState::kUnverified: StatusDot(kWarn); Title(L("Installed, not verified", "Instalado, sin verificar")); break;
    case App::GameState::kIncomplete: StatusDot(kBad); Title(L("Incomplete install", "Instalación incompleta")); break;
    case App::GameState::kMissing: StatusDot(kBad); Title(L("Not installed", "No instalado")); break;
    default: break;
  }
  TextDim(app.game_state_detail.c_str());
  EndCard();

  CardTask(app);

  // Nothing installed yet: only the two ways to get the game in.
  if (!app.Installed()) {
    BeginCard("##iso", L("Import from ISO", "Importar desde la ISO"),
              L("Pick the disc image (.iso) of your copy of Forza Motorsport 2, European or American. About "
                "5 GB are extracted and every file is checked.",
                "Elige la imagen de disco (.iso) de tu copia de Forza Motorsport 2, europea o americana. Se "
                "extraen unos 5 GB y se comprueba cada archivo."));
    ImGui::BeginDisabled(app.TaskRunning());
    if (PrimaryButton(L("Choose ISO and import", "Elegir ISO e importar"), ImVec2(S(260), S(40)))) PickIso(app);
    ImGui::EndDisabled();
    EndCard();
  }

  BeginCard("##folder", L("Game folder", "Carpeta del juego"),
            app.Installed() ? L("Where the game files are. Verify them if something fails.",
                                "Dónde están los archivos del juego. Verifícalos si algo falla.")
                            : L("If the game is already extracted, choose its folder. By default, \"game\" next to the "
                                "launcher.",
                                "Si ya tienes el juego extraído, elige su carpeta. Por defecto, \"game\" junto al "
                                "launcher."));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, kControl);
  std::string dir = app.GameDir().u8string();
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputText("##dir", dir.data(), dir.size() + 1, ImGuiInputTextFlags_ReadOnly);
  ImGui::PopStyleColor();
  ImGui::BeginDisabled(app.TaskRunning() || app.game);
  if (ImGui::Button(L("Change folder", "Cambiar carpeta"), ImVec2(S(170), 0))) {
    SDL_ShowOpenFolderDialog(
        [](void* user, const char* const* files, int) {
          auto* a = static_cast<App*>(user);
          if (files && files[0]) {
            std::lock_guard lock(a->dialog_mutex);
            a->picked_folder = files[0];
          }
        },
        &app, app.window, nullptr, false);
  }
  if (app.Installed()) {
    ImGui::SameLine();
    if (ImGui::Button(L("Verify files", "Verificar archivos"), ImVec2(S(170), 0))) {
      const fs::path game_dir = app.GameDir();
      app.StartTask(L("Verification", "Verificación"), [&app, game_dir] { return app.RunVerify(game_dir); });
      app.task_kind = App::TaskKind::kVerify;
    }
  }
  ImGui::EndDisabled();
  EndCard();

  if (!app.Installed()) return;

  BeginCard("##dlc", L("Downloadable content", "Contenido descargable"),
            L("The 8 premium packs of the Limited Edition (cars, Road America, Motegi).",
              "Los 8 packs premium de la edición limitada (coches, Road America, Motegi)."));
  if (app.pfp.installed) {
    StatusDot(kOk);
    ImGui::TextUnformatted(L("Included with Project Forza Plus 2", "Incluido con Project Forza Plus 2"));
  } else if (app.dlc_packages) {
    StatusDot(kOk);
    ImGui::Text(L("%zu packs added; each profile installs them the next time it plays",
                  "%zu packs añadidos; cada perfil los instala la próxima vez que juega"),
                app.dlc_packages);
  } else {
    StatusDot(kWarn);
    ImGui::TextUnformatted(L("Not added: import the bonus disc ISO, or install Project Forza Plus 2.",
                             "Sin añadir: importa la ISO del disco extra o instala Project Forza Plus 2."));
    ImGui::BeginDisabled(app.TaskRunning() || app.game);
    if (ImGui::Button(L("Import bonus disc ISO", "Importar ISO del disco extra"), ImVec2(S(260), 0))) PickIso(app);
    ImGui::EndDisabled();
  }
  EndCard();

  BeginCard("##shaders", L("Shader cache", "Caché de shaders"),
            L("Shaders are prepared before the first race so it does not stutter. It happens by itself "
              "when you press Play; rebuild it here if something looks wrong.",
              "Los shaders se preparan antes de la primera carrera para que no haya tirones. Se hace solo "
              "al pulsar Jugar; aquí puedes rehacerlo si algo va mal."));
  if (app.NeedsWarmup()) {
    StatusDot(kWarn);
    ImGui::TextUnformatted(L("Will be prepared when you press Play", "Se preparará al pulsar Jugar"));
  } else {
    StatusDot(kOk);
    ImGui::TextUnformatted(L("Ready", "Lista"));
  }
  ImGui::BeginDisabled(app.TaskRunning() || app.game || !app.Installed());
  if (ImGui::Button(L("Prepare shaders now", "Preparar shaders ahora"), ImVec2(S(220), 0))) {
    app.StartWarmup(false);
  }
  ImGui::EndDisabled();
  EndCard();
}

constexpr const char* kKofiUrl = "https://ko-fi.com/post/Forza-Motorsport-2-native-on-PC-E2Q428PZ5Y";

void TabSupport() {
  BeginCard("##support", L("Support the project", "Apoya el proyecto"));
  TextDim(L("This port is a free project made in my spare time. If you enjoy playing Forza Motorsport 2 "
            "natively on PC and want it to keep improving, you can support it on Ko-fi. Every "
            "contribution helps me put more hours into it.",
            "Este port es un proyecto gratuito hecho en mi tiempo libre. Si disfrutas jugando a Forza "
            "Motorsport 2 nativo en PC y quieres que siga mejorando, puedes apoyarlo en Ko-fi. Cada "
            "aportación me ayuda a dedicarle más horas."));
  ImGui::Dummy(ImVec2(0, S(8)));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(10));
  ImGui::PushFont(g_bold, ImGui::GetStyle().FontSizeBase * 1.15f);
  if (PrimaryButton(L("Support on Ko-fi", "Apoyar en Ko-fi"), ImVec2(S(260), S(48)))) {
    SDL_OpenURL(kKofiUrl);
  }
  ImGui::PopFont();
  ImGui::PopStyleVar();
  ImGui::Dummy(ImVec2(0, S(4)));
  TextDim(kKofiUrl);
  ImGui::Dummy(ImVec2(0, S(4)));
  TextDim(L("Thank you!", "¡Gracias!"));
  EndCard();
}

// Underlined text tabs across the top of the settings column.
void TabBar(App& app) {
  const std::pair<Tab, const char*> tabs[] = {{Tab::kProfiles, L("Profiles", "Perfiles")},
                                              {Tab::kGraphics, L("Graphics", "Gráficos")},
                                              {Tab::kControls, L("Controls", "Controles")},
                                              {Tab::kAudio, L("Language & sound", "Idioma y sonido")},
                                              {Tab::kInstall, L("Install", "Instalación")},
                                              {Tab::kForzaPlus, "Forza Plus 2"},
                                              {Tab::kSupport, L("Support", "Apoyar")}};
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImGui::PushFont(g_bold, ImGui::GetStyle().FontSizeBase * 1.05f);
  for (const auto& [tab, label] : tabs) {
    const bool active = app.tab == tab;
    const ImVec2 sz = ImGui::CalcTextSize(label);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    if (ImGui::InvisibleButton("tab", ImVec2(sz.x + S(8), sz.y + S(16)))) {
      app.tab = tab;
      app.capture_index = -1;
      if (tab == Tab::kProfiles) app.RefreshProfiles();
    }
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    dl->AddText(ImVec2(p.x + S(4), p.y + S(4)), Col(active ? kText : (hovered ? kText : kDim)), label);
    if (active) {
      dl->AddRectFilled(ImVec2(p.x + S(4), p.y + sz.y + S(10)), ImVec2(p.x + sz.x + S(4), p.y + sz.y + S(13)),
                        Col(kAccent), S(2));
    }
    ImGui::SameLine(0, S(22));
  }
  ImGui::PopFont();

  // Launcher language, right-aligned: EN | ES.
  const float pill_w = S(42);
  ImGui::SameLine(ImGui::GetContentRegionMax().x - pill_w * 2 - S(4));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(7));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(4), 0));
  for (const auto& [spanish, code] : {std::pair{false, "EN"}, std::pair{true, "ES"}}) {
    const bool active = g_spanish == spanish;
    ImGui::PushStyleColor(ImGuiCol_Button, active ? kAccent : kControl);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? kAccentHover : ImVec4(0.22f, 0.23f, 0.26f, 1));
    if (ImGui::Button(code, ImVec2(pill_w, 0)) && !active) {
      g_spanish = spanish;
      app.SaveLauncherPrefs();
    }
    ImGui::PopStyleColor(2);
    if (!spanish) ImGui::SameLine();
  }
  ImGui::PopStyleVar(2);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  dl->AddLine(ImVec2(p.x, p.y - S(2)), ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y - S(2)),
              Col(ImVec4(1, 1, 1, 0.07f)));
  ImGui::Dummy(ImVec2(0, S(10)));
}

void ApplyStyle() {
  ImGuiStyle& style = ImGui::GetStyle();
  ImGui::StyleColorsDark();
  style.WindowRounding = 0;
  style.ChildRounding = S(12);
  style.FrameRounding = S(8);
  style.PopupRounding = S(10);
  style.GrabRounding = S(8);
  style.ScrollbarRounding = S(8);
  style.TabRounding = S(8);
  style.FramePadding = ImVec2(S(12), S(8));
  style.ItemSpacing = ImVec2(S(10), S(10));
  style.WindowPadding = ImVec2(S(0), S(0));
  style.ScrollbarSize = S(10);
  style.WindowBorderSize = 0;
  style.ChildBorderSize = 0;
  style.PopupBorderSize = 0;
  style.FrameBorderSize = 0;
  ImVec4* c = style.Colors;
  c[ImGuiCol_Text] = kText;
  c[ImGuiCol_TextDisabled] = kDim;
  c[ImGuiCol_WindowBg] = kBg;
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.125f, 0.14f, 0.98f);
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f);
  c[ImGuiCol_FrameBg] = kControl;
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.21f, 0.22f, 0.25f, 1);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.25f, 0.28f, 1);
  c[ImGuiCol_Button] = kControl;
  c[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.23f, 0.26f, 1);
  c[ImGuiCol_ButtonActive] = kAccent;
  c[ImGuiCol_Header] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.30f);
  c[ImGuiCol_HeaderHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.45f);
  c[ImGuiCol_HeaderActive] = kAccent;
  c[ImGuiCol_CheckMark] = kAccent;
  c[ImGuiCol_SliderGrab] = kAccent;
  c[ImGuiCol_SliderGrabActive] = kAccentHover;
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = ImVec4(1, 1, 1, 0.12f);
  c[ImGuiCol_Separator] = ImVec4(1, 1, 1, 0.07f);
  c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] = ImVec4(0, 0, 0, 0);
}

// Headless mode for scripts and tests:
//   Forza2Launcher --import-iso <iso> --game-dir <dir> [--log <file>]
//   Forza2Launcher --verify --game-dir <dir> [--log <file>]
//   Forza2Launcher --install-pfp <zip> [--pfp-update <zip>] [--pfp-mode 0-3] [--pfp-harder-ai]
//                  --game-dir <dir> [--log <file>]
//   Forza2Launcher --update-pfp <zip> --game-dir <dir> [--log <file>]
//   Forza2Launcher --uninstall-pfp --game-dir <dir> [--log <file>]
//   Forza2Launcher --pfp-fov <12 comma-separated degrees> --game-dir <dir> [--log <file>]
//   Forza2Launcher --pfp-physics <fps> --game-dir <dir> [--log <file>]
// Same code paths as the UI; prints progress to the log, exit code 0 = ok.
int RunHeadless(int argc, char** argv) {
  std::string iso, dir, log_path, pfp_zip, pfp_update;
  bool verify_only = false, pfp_uninstall = false, pfp_harder_ai = false;
  int pfp_mode = 0;
  std::string pfp_fov, pfp_update_only;
  int pfp_physics = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--import-iso" && i + 1 < argc) iso = argv[++i];
    else if (a == "--game-dir" && i + 1 < argc) dir = argv[++i];
    else if (a == "--log" && i + 1 < argc) log_path = argv[++i];
    else if (a == "--verify") verify_only = true;
    else if (a == "--install-pfp" && i + 1 < argc) pfp_zip = argv[++i];
    else if (a == "--pfp-update" && i + 1 < argc) pfp_update = argv[++i];
    else if (a == "--pfp-mode" && i + 1 < argc) pfp_mode = std::clamp(std::atoi(argv[++i]), 0, 3);
    else if (a == "--pfp-harder-ai") pfp_harder_ai = true;
    else if (a == "--uninstall-pfp") pfp_uninstall = true;
    else if (a == "--pfp-fov" && i + 1 < argc) pfp_fov = argv[++i];
    else if (a == "--update-pfp" && i + 1 < argc) pfp_update_only = argv[++i];
    else if (a == "--pfp-physics" && i + 1 < argc) pfp_physics = std::atoi(argv[++i]);
  }
  std::ofstream log(log_path.empty() ? "launcher-headless.log" : log_path);
  const fs::path exe_dir = fs::u8path(SDL_GetBasePath());
  const auto manifest = LoadManifest(exe_dir / "game_files.sha256");
  if (manifest.empty() || dir.empty()) {
    log << "error: missing game_files.sha256 or --game-dir\n";
    return 2;
  }
  TaskProgress progress;
  std::atomic<bool> finished{false};
  std::thread reporter([&] {
    while (!finished) {
      std::this_thread::sleep_for(std::chrono::seconds(5));
      log << progress.done / 1000000 << "/" << progress.total / 1000000 << " MB  " << progress.Status()
          << std::endl;
    }
  });
  const auto start = std::chrono::steady_clock::now();
  bool ok = true;
  bool verify = true;
  if (pfp_physics > 0) {
    const std::string error = SetForzaPlusPhysicsRate(fs::u8path(dir), pfp_physics);
    ok = error.empty();
    log << "pfp-physics: " << (ok ? "ok" : error) << std::endl;
  } else if (!pfp_update_only.empty()) {
    ok = UpdateForzaPlus(fs::u8path(dir), fs::u8path(pfp_update_only), progress);
    log << "update-pfp: " << (ok ? "ok" : "FAILED") << " - " << progress.Status() << std::endl;
  } else if (!pfp_fov.empty()) {
    PfpFov fov;
    std::istringstream values(pfp_fov);
    std::string value;
    for (int i = 0; i < kCameraCount * 3 && std::getline(values, value, ','); ++i) {
      fov.degrees[i / 3][i % 3] = std::atoi(value.c_str());
    }
    const std::string error = SetForzaPlusFov(fs::u8path(dir), fov);
    ok = error.empty();
    log << "pfp-fov: " << (ok ? "ok" : error) << std::endl;
  } else if (!pfp_zip.empty() || pfp_uninstall) {
    ok = pfp_uninstall ? UninstallForzaPlus(fs::u8path(dir), progress)
                       : InstallForzaPlus(fs::u8path(dir), fs::u8path(pfp_zip), fs::u8path(pfp_update),
                                          PfpMode(pfp_mode), pfp_harder_ai, progress);
    log << (pfp_uninstall ? "uninstall-pfp: " : "install-pfp: ") << (ok ? "ok" : "FAILED") << " - "
        << progress.Status() << std::endl;
  } else if (!verify_only) {
    const DiscKind kind = ExtractIso(fs::u8path(iso), fs::u8path(dir), progress);
    ok = kind != DiscKind::kFailed;
    verify = kind == DiscKind::kGame;  // the bonus disc only adds DLC packages
    log << "extract: " << (ok ? "ok" : "FAILED") << " - " << progress.Status() << std::endl;
    if (verify && LoadForzaPlus(fs::u8path(dir)).installed) ok = UninstallForzaPlus(fs::u8path(dir), progress);
  }
  if (ok && verify) {
    const VerifyReport report =
        VerifyGame(fs::u8path(dir), WithoutModFiles(manifest, LoadForzaPlus(fs::u8path(dir))), true, progress);
    ok = report.ok();
    log << "verify: " << (ok ? "ok" : "FAILED") << " missing=" << report.missing.size()
        << " size=" << report.wrong_size.size() << " hash=" << report.wrong_hash.size() << std::endl;
    for (const auto* list : {&report.missing, &report.wrong_size, &report.wrong_hash}) {
      for (const auto& p : *list) log << "  " << p << "\n";
    }
    if (ok) std::ofstream(VerifiedMarker(fs::u8path(dir))) << "verified against game_files.sha256\n";
  }
  finished = true;
  reporter.join();
  log << "seconds: "
      << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count()
      << std::endl;
  return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--import-iso" || a == "--verify" || a == "--install-pfp" || a == "--uninstall-pfp" ||
        a == "--pfp-fov" || a == "--update-pfp" || a == "--pfp-physics") {
      return RunHeadless(argc, argv);
    }
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Forza2Launcher", SDL_GetError(), nullptr);
    return 1;
  }
  App app;
  app.exe_dir = fs::u8path(SDL_GetBasePath());
  app.user_config = app.exe_dir / "forza2.user.toml";
  app.manifest_path = app.exe_dir / "game_files.sha256";
  app.game_exe = app.exe_dir / "forza2.exe";
  const char* documents = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
  app.profiles_root = fs::u8path(documents ? documents : SDL_GetBasePath()) / "forza2" / "profiles";
  app.launcher_config = app.exe_dir / "launcher.toml";
  app.bundled_cache_dir = app.exe_dir / "shadercache";
  app.shader_cache_dir = fs::u8path(documents ? documents : SDL_GetBasePath()) / "forza2" / "cache" /
                         "shaders" / "shareable";
  app.LoadLauncherPrefs();
  app.settings.Load(app.user_config);
  app.manifest = LoadManifest(app.manifest_path);
  if (app.manifest.empty()) {
    app.notice = L("game_files.sha256 is missing next to the launcher: the game cannot be verified.",
                   "Falta game_files.sha256 junto al launcher: no se puede verificar el juego.");
  }
  app.RefreshProfiles();
  app.CheckGame();
  if (!app.Installed()) app.tab = Tab::kInstall;

  g_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  if (g_scale <= 0) g_scale = 1.0f;
  // Fixed size, set once the cover is loaded (its aspect decides the height).
  app.window = SDL_CreateWindow("Forza Motorsport 2", int(S(1200)), int(S(850)),
                                SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  // The SDK's SDL3 is built without the 2D renderer; SDL_GPU (D3D12 on
  // Windows, Vulkan elsewhere) is what it has.
  SDL_GPUDevice* gpu = SDL_CreateGPUDevice(
      SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
  if (!gpu || !SDL_ClaimWindowForGPUDevice(gpu, app.window)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Forza2Launcher",
                             (std::string(L("Could not start the GPU: ", "No se pudo iniciar la GPU: ")) + SDL_GetError()).c_str(),
                             app.window);
    return 1;
  }
  SDL_SetGPUSwapchainParameters(gpu, app.window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                SDL_GPU_PRESENTMODE_VSYNC);
  const SDL_GPUTextureFormat swap_format = SDL_GetGPUSwapchainTextureFormat(gpu, app.window);
  for (const char* name : {"cover.png", "cover.bmp"}) {
    if (fs::exists(app.exe_dir / name) && LoadCover(gpu, app.exe_dir / name, app.cover)) break;
  }
  SDL_SetWindowSize(app.window, int(CoverWidth() + SettingsWidth()),
                    int(CoverHeight(app) + ControlsHeight()));
  SDL_SetWindowPosition(app.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(app.window);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  ApplyStyle();
  const float font_size = 17.0f * g_scale;
  const char* regular = "C:\\Windows\\Fonts\\segoeui.ttf";
  const char* bold = "C:\\Windows\\Fonts\\seguisb.ttf";
  // Segoe UI lacks arrows and a few symbols (↺ ← ↑); merge Segoe UI Symbol in.
  const char* symbols = "C:\\Windows\\Fonts\\seguisym.ttf";
  ImFontConfig merge;
  merge.MergeMode = true;
  if (fs::exists(regular)) {
    io.Fonts->AddFontFromFileTTF(regular, font_size);
    if (fs::exists(symbols)) io.Fonts->AddFontFromFileTTF(symbols, font_size, &merge);
  }
  if (fs::exists(bold)) {
    g_bold = io.Fonts->AddFontFromFileTTF(bold, font_size);
    if (fs::exists(symbols)) io.Fonts->AddFontFromFileTTF(symbols, font_size, &merge);
  }
  ImGui::GetStyle().FontSizeBase = font_size;
  ImGui_ImplSDL3_InitForSDLGPU(app.window);
  ImGui_ImplSDLGPU3_InitInfo gpu_info = {};
  gpu_info.Device = gpu;
  gpu_info.ColorTargetFormat = swap_format;
  gpu_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
  ImGui_ImplSDLGPU3_Init(&gpu_info);

  // --screenshot <file.bmp> [--tab profiles|graphics|controls|audio|install|forzaplus]:
  // render a few frames, save the window and exit (docs and UI checks).
  std::string screenshot;
  int screenshot_frame = 4;
  for (int i = 1; i + 1 < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--screenshot") screenshot = argv[i + 1];
    if (a == "--screenshot-frame") screenshot_frame = std::max(4, std::atoi(argv[i + 1]));
    if (a == "--tab" || a == "--page") {
      const std::string t = argv[i + 1];
      app.tab = t == "graphics" ? Tab::kGraphics
                : t == "controls" ? Tab::kControls
                : t == "audio"    ? Tab::kAudio
                : t == "install"  ? Tab::kInstall
                : t == "forzaplus" ? Tab::kForzaPlus
                : t == "support"  ? Tab::kSupport
                                  : Tab::kProfiles;
    }
  }
  int frames = 0;

  // --play: press JUGAR right away (desktop shortcuts straight into the game).
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--play" && app.Installed()) {
      if (app.NeedsWarmup()) {
        app.StartWarmup(true);
      } else {
        app.LaunchGame();
      }
    }
    // --prepare-shaders: warm the shader cache without starting the game.
    if (a == "--prepare-shaders" && app.Installed()) app.StartWarmup(false);
    // --start-import <iso>: same as picking the ISO in the Install tab (tests).
    if (a == "--start-import" && i + 1 < argc) {
      std::lock_guard lock(app.dialog_mutex);
      app.picked_iso = argv[i + 1];
    }
  }

  bool running = true;
  while (running) {
    const bool capture_now = !screenshot.empty() && ++frames == screenshot_frame;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (app.capture_index >= 0 && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        auto& bind = app.settings.binds[app.capture_index];
        if (event.key.scancode != SDL_SCANCODE_ESCAPE) {
          std::string name = KeyName(event.key.scancode);
          if (!name.empty()) {
            if ((event.key.mod & SDL_KMOD_SHIFT) && name != "Shift") name = "Shift+" + name;
            bind.keys = app.capture_append && !bind.keys.empty() ? bind.keys + "," + name : name;
            app.Save();
          }
        }
        app.capture_index = -1;
        continue;  // don't let ImGui see the captured key
      }
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) running = false;
    }
    if (SDL_GetWindowFlags(app.window) & SDL_WINDOW_MINIMIZED) {
      app.PollGame();
      SDL_Delay(100);
      continue;
    }

    // Results from file dialogs.
    {
      std::lock_guard lock(app.dialog_mutex);
      if (app.picked_folder) {
        app.settings.game_data_root = *app.picked_folder;
        app.picked_folder.reset();
        app.Save();
        app.CheckGame();
      }
      if (app.picked_pfp_base) {
        app.pfp_base_zip = *app.picked_pfp_base;
        app.picked_pfp_base.reset();
      }
      if (app.picked_pfp_update) {
        app.pfp_update_zip = *app.picked_pfp_update;
        app.picked_pfp_update.reset();
      }
      if (app.picked_iso) {
        const fs::path iso = fs::u8path(*app.picked_iso);
        const fs::path dest = app.GameDir();
        app.picked_iso.reset();
        app.Save();
        app.StartTask(L("Import", "Importación"), [&app, iso, dest] {
          app.import_step = 1;
          const DiscKind kind = ExtractIso(iso, dest, app.task);
          if (kind == DiscKind::kFailed || app.task.cancel) return false;
          if (kind == DiscKind::kBonus) return true;
          // The game disc put the original files back over Project Forza Plus 2:
          // drop the rest of the mod so the folder is consistent.
          if (LoadForzaPlus(dest).installed && !UninstallForzaPlus(dest, app.task)) return false;
          app.import_step = 2;
          return app.RunVerify(dest);
        });
        app.task_kind = App::TaskKind::kImport;
      }
    }
    static TaskProgress::State last_state = TaskProgress::State::kIdle;
    if (last_state == TaskProgress::State::kRunning && !app.TaskRunning()) {
      app.CheckGame();
      if (app.launch_after_task && app.task.state == TaskProgress::State::kDone) app.LaunchGame();
      app.launch_after_task = false;
    }
    last_state = app.task.state;
    app.PollGame();

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);

    // Left: the cover column (fixed width, the cover shown whole).
    const float cover_w = CoverWidth();
    ImGui::BeginChild("##cover", ImVec2(cover_w, vp->Size.y), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    CoverPanel(app, ImVec2(cover_w, vp->Size.y));
    ImGui::EndChild();
    ImGui::SameLine(0, 0);

    // Right: tabs + content.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(34), S(26)));
    ImGui::BeginChild("##settings", ImVec2(0, vp->Size.y), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    TabBar(app);
    if (!app.notice.empty()) {
      TextColored(kWarn, app.notice.c_str());
      ImGui::SameLine();
      if (ImGui::SmallButton(L("Dismiss", "Cerrar"))) app.notice.clear();
    }
    ImGui::BeginChild("##content", ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20), S(16)));
    switch (app.tab) {
      case Tab::kProfiles: TabProfiles(app); break;
      case Tab::kGraphics: TabGraphics(app); break;
      case Tab::kControls: TabControls(app); break;
      case Tab::kAudio: TabAudio(app); break;
      case Tab::kInstall: TabInstall(app); break;
      case Tab::kForzaPlus: TabForzaPlus(app); break;
      case Tab::kSupport: TabSupport(); break;
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::End();

    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu);
    SDL_GPUTexture* swapchain = nullptr;
    Uint32 width = 0, height = 0;
    SDL_WaitAndAcquireGPUSwapchainTexture(cmd, app.window, &swapchain, &width, &height);
    // Screenshot mode draws into an offscreen copy that can be downloaded.
    SDL_GPUTexture* target = swapchain;
    SDL_GPUTexture* offscreen = nullptr;
    if (capture_now && swapchain) {
      SDL_GPUTextureCreateInfo info = {};
      info.type = SDL_GPU_TEXTURETYPE_2D;
      info.format = swap_format;
      info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
      info.width = width;
      info.height = height;
      info.layer_count_or_depth = 1;
      info.num_levels = 1;
      offscreen = SDL_CreateGPUTexture(gpu, &info);
      target = offscreen;
    }
    if (target && draw_data->DisplaySize.x > 0) {
      ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, cmd);
      SDL_GPUColorTargetInfo color = {};
      color.texture = target;
      color.clear_color = SDL_FColor{kBg.x, kBg.y, kBg.z, 1.0f};
      color.load_op = SDL_GPU_LOADOP_CLEAR;
      color.store_op = SDL_GPU_STOREOP_STORE;
      SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &color, 1, nullptr);
      ImGui_ImplSDLGPU3_RenderDrawData(draw_data, cmd, pass);
      SDL_EndGPURenderPass(pass);
    }
    if (offscreen) {
      SDL_GPUTransferBufferCreateInfo tb_info = {};
      tb_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
      tb_info.size = width * height * 4;
      SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(gpu, &tb_info);
      SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
      SDL_GPUTextureRegion region = {};
      region.texture = offscreen;
      region.w = width;
      region.h = height;
      region.d = 1;
      SDL_GPUTextureTransferInfo dst = {};
      dst.transfer_buffer = tb;
      SDL_DownloadFromGPUTexture(copy, &region, &dst);
      SDL_EndGPUCopyPass(copy);
      SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
      SDL_WaitForGPUFences(gpu, true, &fence, 1);
      SDL_ReleaseGPUFence(gpu, fence);
      void* pixels = SDL_MapGPUTransferBuffer(gpu, tb, false);
      SDL_Surface* shot = SDL_CreateSurfaceFrom(int(width), int(height),
                                                SDL_GetPixelFormatFromGPUTextureFormat(swap_format),
                                                pixels, int(width * 4));
      if (!shot || !SDL_SaveBMP(shot, screenshot.c_str())) {
        std::ofstream(screenshot + ".txt") << "screenshot failed: " << SDL_GetError() << "\n";
      }
      SDL_DestroySurface(shot);
      SDL_UnmapGPUTransferBuffer(gpu, tb);
      SDL_ReleaseGPUTransferBuffer(gpu, tb);
      SDL_ReleaseGPUTexture(gpu, offscreen);
      running = false;
    } else {
      SDL_SubmitGPUCommandBuffer(cmd);
    }
  }

  app.task.cancel = true;
  if (app.worker.joinable()) app.worker.join();
  SDL_WaitForGPUIdle(gpu);
  ImGui_ImplSDL3_Shutdown();
  ImGui_ImplSDLGPU3_Shutdown();
  ImGui::DestroyContext();
  if (app.cover.texture) SDL_ReleaseGPUTexture(gpu, app.cover.texture);
  if (app.cover.sampler) SDL_ReleaseGPUSampler(gpu, app.cover.sampler);
  SDL_ReleaseWindowFromGPUDevice(gpu, app.window);
  SDL_DestroyGPUDevice(gpu);
  SDL_DestroyWindow(app.window);
  SDL_Quit();
  return 0;
}
