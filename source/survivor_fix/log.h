#pragma once

namespace sf {

// debug.log next to the game, written only when logging is enabled in the configuration window.
void LogOpen(const wchar_t* path, bool enabled);
void Log(const char* fmt, ...);

}  // namespace sf
