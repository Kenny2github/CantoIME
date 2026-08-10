// Settings.h
//
// Per-user CantoIME settings, stored under HKCU\Software\CantoIME. Edited via the
// "Keyboard options" dialog (see OptionsDialog) and read at activation.

#pragma once

#include <string>

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
};

// Reads settings from HKCU, substituting defaults for any missing value.
CantoSettings LoadSettings();

// Writes settings to HKCU (creating the key). Returns true on success.
bool SaveSettings(const CantoSettings& s);
