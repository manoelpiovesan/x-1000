#pragma once
#include <filesystem>
#include <vector>

namespace xpad::gui {

// Opens the OS file picker for audio files (multi-select).
// Windows: native dialog. Linux: zenity or kdialog, if installed.
// Returns an empty list when cancelled or when no picker is available.
std::vector<std::filesystem::path> pickAudioFiles();

} // namespace xpad::gui
