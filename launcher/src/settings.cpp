#include "settings.h"

#include <fstream>

#include <toml++/toml.hpp>

namespace launcher {

namespace {

// Defaults are the runtime's own (src/input/mnk/mnk_input_driver.cpp), except
// throttle/brake which also get E/Q as their first, easiest key.
const KeyBind kDefaultBinds[] = {
    {"right_trigger", "Accelerate  (RT)", "Acelerar  (RT)", "Driving", "Conducción", "E,O", {}},
    {"left_trigger", "Brake  (LT)", "Frenar  (LT)", "Driving", "Conducción", "Q,I", {}},
    {"lstick_left", "Steer left", "Girar a la izquierda", "Driving", "Conducción", "A", {}},
    {"lstick_right", "Steer right", "Girar a la derecha", "Driving", "Conducción", "D", {}},
    {"lstick_up", "Left stick up", "Stick izquierdo arriba", "Driving", "Conducción", "W", {}},
    {"lstick_down", "Left stick down", "Stick izquierdo abajo", "Driving", "Conducción", "S", {}},
    {"a", "A", "A", "Buttons", "Botones", "Semicolon,Space", {}},
    {"b", "B", "B", "Buttons", "Botones", "Quote,Backspace", {}},
    {"x", "X", "X", "Buttons", "Botones", "L", {}},
    {"y", "Y", "Y", "Buttons", "Botones", "P", {}},
    {"left_shoulder", "LB", "LB", "Buttons", "Botones", "1", {}},
    {"right_shoulder", "RB", "RB", "Buttons", "Botones", "3", {}},
    {"start", "Start", "Start", "Buttons", "Botones", "X,Return", {}},
    {"back", "Back", "Back", "Buttons", "Botones", "Z,Tab", {}},
    {"lstick_press", "Left stick click", "Pulsar stick izquierdo", "Buttons", "Botones", "F", {}},
    {"dpad_up", "Up", "Arriba", "D-pad", "Cruceta", "Shift+Up", {}},
    {"dpad_down", "Down", "Abajo", "D-pad", "Cruceta", "Shift+Down", {}},
    {"dpad_left", "Left", "Izquierda", "D-pad", "Cruceta", "Shift+Left", {}},
    {"dpad_right", "Right", "Derecha", "D-pad", "Cruceta", "Shift+Right", {}},
    {"rstick_up", "Look up", "Mirar arriba", "Camera", "Cámara", "Up", {}},
    {"rstick_down", "Look down", "Mirar abajo", "Camera", "Cámara", "Down", {}},
    {"rstick_left", "Look left", "Mirar a la izquierda", "Camera", "Cámara", "Left", {}},
    {"rstick_right", "Look right", "Mirar a la derecha", "Camera", "Cámara", "Right", {}},
    {"rstick_press", "Right stick click", "Pulsar stick derecho", "Camera", "Cámara", "K", {}},
};

template <typename T>
void Read(const toml::table& t, const char* key, T& out) {
  if (auto v = t[key].value<T>()) out = *v;
}

}  // namespace

Settings::Settings() { ResetBinds(); }

void Settings::ResetBinds() {
  binds.assign(std::begin(kDefaultBinds), std::end(kDefaultBinds));
  for (auto& b : binds) b.keys = b.default_keys;
}

void Settings::ResetDisplay() {
  Settings defaults;
  fullscreen = defaults.fullscreen;
  monitor = defaults.monitor;
  window_width = defaults.window_width;
  window_height = defaults.window_height;
  refresh_rate = defaults.refresh_rate;
  present_letterbox = defaults.present_letterbox;
  resolution_scale = defaults.resolution_scale;
  anisotropic_override = defaults.anisotropic_override;
  native_2x_msaa = defaults.native_2x_msaa;
  swap_post_effect = defaults.swap_post_effect;
  present_effect = defaults.present_effect;
}

bool Settings::Load(const std::filesystem::path& file) {
  toml::table t;
  try {
    t = toml::parse_file(file.u8string());
  } catch (const toml::parse_error&) {
    return false;
  }
  Read(t, "game_data_root", game_data_root);
  Read(t, "gamertag", gamertag);
  int64_t language = user_language;
  Read(t, "user_language", language);
  user_language = int(language);

  Read(t, "fullscreen", fullscreen);
  int64_t v = monitor;
  Read(t, "monitor", v), monitor = int(v);
  v = window_width, Read(t, "window_width", v), window_width = v > 0 ? int(v) : window_width;
  v = window_height, Read(t, "window_height", v), window_height = v > 0 ? int(v) : window_height;
  // Older files may carry vsync = false (unpaced); that is not offered any
  // more, so only the refresh rate is read, snapped to the offered values.
  double hz = refresh_rate;
  Read(t, "video_mode_refresh_rate", hz);
  refresh_rate = hz >= 90 ? 120 : 60;
  Read(t, "present_letterbox", present_letterbox);

  v = resolution_scale, Read(t, "resolution_scale", v), resolution_scale = int(v);
  v = anisotropic_override, Read(t, "anisotropic_override", v), anisotropic_override = int(v);
  Read(t, "native_2x_msaa", native_2x_msaa);
  Read(t, "swap_post_effect", swap_post_effect);
  Read(t, "present_effect", present_effect);

  Read(t, "input_backend", input_backend);
  Read(t, "mnk_mode", mnk_mode);
  Read(t, "mnk_mouse", mnk_mouse);
  Read(t, "mnk_sensitivity", mnk_sensitivity);
  for (auto& b : binds) {
    Read(t, (std::string("keybind_") + b.id).c_str(), b.keys);
  }
  Read(t, "audio_mute", audio_mute);
  return true;
}

bool Settings::Save(const std::filesystem::path& file) const {
  toml::table t;
  t.insert("game_data_root", game_data_root);
  t.insert("gamertag", gamertag);
  t.insert("user_language", user_language);

  t.insert("fullscreen", fullscreen);
  t.insert("monitor", monitor);
  t.insert("window_width", window_width);
  t.insert("window_height", window_height);
  t.insert("vsync", true);
  t.insert("video_mode_refresh_rate", double(refresh_rate));
  t.insert("present_letterbox", present_letterbox);

  t.insert("resolution_scale", resolution_scale);
  t.insert("anisotropic_override", anisotropic_override);
  t.insert("native_2x_msaa", native_2x_msaa);
  t.insert("swap_post_effect", swap_post_effect);
  t.insert("present_effect", present_effect);

  t.insert("input_backend", input_backend);
  t.insert("mnk_mode", mnk_mode);
  t.insert("mnk_mouse", mnk_mouse);
  t.insert("mnk_sensitivity", mnk_sensitivity);
  for (const auto& b : binds) {
    t.insert(std::string("keybind_") + b.id, b.keys);
  }
  t.insert("audio_mute", audio_mute);

  std::ofstream out(file, std::ios::trunc);
  if (!out) return false;
  out << "# Written by Forza2Launcher. The game loads this after forza2.toml,\n"
         "# so these values win. Edit from the launcher.\n\n"
      << t << "\n";
  return bool(out);
}

bool IsValidGamertag(const std::string& name) {
  if (name.empty() || name.size() > 15 || name.front() == ' ' || name.back() == ' ') {
    return false;
  }
  char prev = 0;
  for (char c : name) {
    const bool alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (!alnum && !(c == ' ' && prev != ' ')) return false;
    prev = c;
  }
  return true;
}

}  // namespace launcher
