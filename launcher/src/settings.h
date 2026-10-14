#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace launcher {

// One keyboard binding of the virtual pad (cvar keybind_<id>). `keys` uses the
// runtime's syntax: comma-separated alternatives, optional "Shift+" prefix.
struct KeyBind {
  const char* id;      // cvar suffix
  const char* label;     // shown in the launcher (English)
  const char* label_es;  // ... (Spanish)
  const char* group;     // section in the Controls page (English)
  const char* group_es;
  const char* default_keys;
  std::string keys;
};

// Everything the launcher lets the player change. Written to
// forza2.user.toml, which the game loads after its own forza2.toml.
struct Settings {
  // Game / profile
  std::string game_data_root;
  std::string gamertag = "Player";
  int user_language = 1;  // XC_LANGUAGE: 1 en, 3 de, 4 fr, 5 es, 6 it

  // Display
  bool fullscreen = true;
  int monitor = 0;
  int window_width = 1600;
  int window_height = 900;
  // Guest video refresh (video_mode_refresh_rate): 60 or 120 Hz. The title
  // paces itself on those vblanks; `vsync` always stays on, since without it
  // the runtime fires a vblank every millisecond and the game runs unpaced,
  // with constant hitches. 144 Hz is not offered: the title then waits two
  // vblanks per frame in races (72 fps target) and held only 52-56 fps.
  int refresh_rate = 60;
  bool present_letterbox = true;

  // Quality
  int resolution_scale = 1;      // internal render scale over 1280x720
  int anisotropic_override = 3;  // -1 off-override, 0..5 = off,1x,2x,4x,8x,16x
  bool native_2x_msaa = true;
  std::string swap_post_effect = "none";   // none | fxaa | fxaa_extreme
  std::string present_effect = "bilinear";  // bilinear | cas | fsr

  // Input
  std::string input_backend = "sdl";  // sdl | xinput
  bool mnk_mode = true;
  bool mnk_mouse = false;
  double mnk_sensitivity = 1.0;
  std::vector<KeyBind> binds;

  // Audio
  bool audio_mute = false;

  Settings();
  void ResetBinds();
  void ResetDisplay();
  bool Load(const std::filesystem::path& file);
  bool Save(const std::filesystem::path& file) const;
};

// Gamertag rules shared with the game (src/profile.cpp): 1-15 letters,
// digits and single spaces.
bool IsValidGamertag(const std::string& name);

}  // namespace launcher
