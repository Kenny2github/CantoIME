// Settings.h
//
// Per-user CantoIME settings, stored under HKCU\Software\CantoIME. Edited via the
// "Keyboard options" dialog (see OptionsDialog) and read at activation.

#pragma once

#include <string>
#include <vector>
#include <utility>

// Which key, tapped on its own, toggles Chinese <-> English input mode.
enum class ToggleKeyMode
{
    Neither    = 0,
    LeftShift  = 1,
    RightShift = 2,
    Both       = 3, // either shift (default)
};

// Which dictionary to read. This is the sole mechanism that selects the
// dictionary (see ResolveDictionaryPath). When unset, LoadSettings defaults to
// the variant's script (Traditional for HK/TW/MO, Simplified for CN/SG).
enum class DictSource
{
    StockTraditional = 0, // the bundled traditional.txt next to the DLL
    StockSimplified  = 1, // the bundled simplified.txt next to the DLL
    Custom           = 2, // the file named by dictionaryPath
};

struct CantoSettings
{
    DictSource    dictSource = DictSource::StockTraditional;

    // Custom dictionary path, used only when dictSource == Custom. May contain
    // environment variables (e.g. %APPDATA%).
    std::wstring  dictionaryPath;

    // Candidate window font cell height, in pixels. Default CANTO_DEFAULT_FONT_SIZE.
    int           fontSize = 0; // 0 -> filled with the default by LoadSettings

    // Which shift key, tapped alone, toggles Chinese/English. Default: either.
    ToggleKeyMode toggleKey = ToggleKeyMode::Both;
};

// Reads settings from HKCU, substituting defaults for any missing value.
CantoSettings LoadSettings();

// Writes settings to HKCU (creating the key). Returns true on success.
bool SaveSettings(const CantoSettings& s);

// A user override: a romanization key and the extra candidate(s) to append
// after the stock dictionary's candidates for that key. Managed entirely from
// the Keyboard options dialog (no text-file editing) and merged into the
// dictionary at load (see Dictionary::ApplyOverrides).
using CantoOverride = std::pair<std::wstring, std::vector<std::wstring>>;

// Reads/writes overrides under HKCU\Software\CantoIME\Overrides, one REG_SZ
// value per key holding the space-separated candidate list. LoadOverrides
// returns them sorted by key for a stable UI order.
std::vector<CantoOverride> LoadOverrides();
bool SaveOverrides(const std::vector<CantoOverride>& overrides);
