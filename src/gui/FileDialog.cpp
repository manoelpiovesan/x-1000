#include "gui/FileDialog.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

namespace xpad::gui {

#ifdef _WIN32

std::vector<std::filesystem::path> pickAudioFiles() {
    std::vector<wchar_t> buffer(32768, L'\0');
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.lpstrFilter = L"Audio files (*.wav;*.flac;*.mp3;*.ogg)\0*.wav;*.flac;*.mp3;*.ogg\0All files\0*.*\0";
    ofn.lpstrTitle = L"Import samples";
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    std::vector<std::filesystem::path> result;
    if (!GetOpenFileNameW(&ofn)) return result;

    // Single file: "path\0\0". Several: "dir\0file1\0file2\0\0".
    const wchar_t* first = buffer.data();
    const std::wstring head(first);
    const wchar_t* next = first + head.size() + 1;
    if (*next == L'\0') {
        result.emplace_back(head);
        return result;
    }
    while (*next != L'\0') {
        const std::wstring name(next);
        result.emplace_back(std::filesystem::path(head) / name);
        next += name.size() + 1;
    }
    return result;
}

#else

namespace {

bool commandExists(const char* name) {
    const std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

std::string runAndRead(const std::string& cmd) {
    std::string out;
    if (FILE* pipe = popen(cmd.c_str(), "r")) {
        char chunk[512];
        while (std::fgets(chunk, sizeof(chunk), pipe)) out += chunk;
        pclose(pipe);
    }
    return out;
}

} // namespace

std::vector<std::filesystem::path> pickAudioFiles() {
    std::string out;
    char sep = '\n';
    if (commandExists("zenity")) {
        out = runAndRead("zenity --file-selection --multiple --separator='|' --title='Import samples' "
                         "--file-filter='Audio | *.wav *.flac *.mp3 *.ogg' 2>/dev/null");
        sep = '|';
    } else if (commandExists("kdialog")) {
        out = runAndRead("kdialog --getopenfilename . '*.wav *.flac *.mp3 *.ogg|Audio' "
                         "--multiple --separate-output 2>/dev/null");
    }

    std::vector<std::filesystem::path> result;
    std::size_t start = 0;
    while (start <= out.size()) {
        std::size_t end = out.find(sep, start);
        if (end == std::string::npos) end = out.size();
        std::string item = out.substr(start, end - start);
        while (!item.empty() && (item.back() == '\n' || item.back() == '\r')) item.pop_back();
        if (!item.empty()) result.emplace_back(item);
        start = end + 1;
    }
    return result;
}

#endif

} // namespace xpad::gui
