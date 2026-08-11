// Globals.h
//
// Shared globals, GUIDs, and constants for CantoIME.
//
// This is a Windows 11 TSF (Text Services Framework) text service. It is an
// in-proc COM DLL. All the GUIDs below MUST be unique to this text service.
//
// CantoIME ships one DLL per Traditional Chinese variant Windows offers. The
// build selects exactly one variant per DLL by defining CANTO_VARIANT_HK,
// CANTO_VARIANT_TW, or CANTO_VARIANT_MO (default: HK). Each variant gets its
// own CLSID, profile GUID, LANGID, description, and window class so the DLLs
// register as independent input methods and can be installed side by side.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <msctf.h>   // TSF core interfaces
#include <string>

// ---------------------------------------------------------------------------
// Variant selection
// ---------------------------------------------------------------------------

// Exactly one CANTO_VARIANT_* must be defined. Traditional: HK/TW/MO. Simplified:
// CN/SG. Default to Hong Kong SAR when the build did not specify one.
#if !defined(CANTO_VARIANT_HK) && !defined(CANTO_VARIANT_TW) && \
    !defined(CANTO_VARIANT_MO) && !defined(CANTO_VARIANT_CN) && \
    !defined(CANTO_VARIANT_SG)
#  define CANTO_VARIANT_HK
#endif

#if defined(CANTO_VARIANT_HK)
#  define TEXTSERVICE_LANGID  MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_HONGKONG)    // 0x0C04
#  define CANTO_VARIANT_TAG   L"HK"
#elif defined(CANTO_VARIANT_TW)
#  define TEXTSERVICE_LANGID  MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL) // 0x0404
#  define CANTO_VARIANT_TAG   L"TW"
#elif defined(CANTO_VARIANT_MO)
#  define TEXTSERVICE_LANGID  MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_MACAU)       // 0x1404
#  define CANTO_VARIANT_TAG   L"MO"
#elif defined(CANTO_VARIANT_CN)
#  define TEXTSERVICE_LANGID  MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)  // 0x0804
#  define CANTO_VARIANT_TAG   L"CN"
#elif defined(CANTO_VARIANT_SG)
#  define TEXTSERVICE_LANGID  MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SINGAPORE)   // 0x1004
#  define CANTO_VARIANT_TAG   L"SG"
#endif

// 1 for Simplified variants (CN/SG), else 0. Selects the default stock dictionary
// when the user has not chosen one (see Settings / ResolveDictionaryPath).
#if defined(CANTO_VARIANT_CN) || defined(CANTO_VARIANT_SG)
#  define CANTO_IS_SIMPLIFIED 1
#else
#  define CANTO_IS_SIMPLIFIED 0
#endif

// Display name shown in the input switcher. The variant is already conveyed by
// the Windows language it registers under, so this is just the product name.
#define TEXTSERVICE_DESC    L"CantoIME"

// ---------------------------------------------------------------------------
// Text service identity
// ---------------------------------------------------------------------------

// CLSID of the text service COM object. Distinct per variant (see Globals.cpp).
extern const CLSID CLSID_CantoIME;

// Language profile GUID. Each (LANGID, GUID_Profile) pair is a distinct
// input method entry that Windows shows in the input switcher. Distinct per
// variant (see Globals.cpp).
extern const GUID GUID_Profile;

// Language-bar item GUID for the main input-method button (the input-indicator
// glyph). Language-bar items are keyed by (clsidService, guidItem); clsidService
// is per-variant, so one shared item GUID is fine. The input-mode (中/英) button
// uses the well-known GUID_LBI_INPUTMODE instead.
extern const GUID GUID_CantoMainButton;

// Language-bar item GUID for the settings button (a left-click opens the options
// dialog; works in the normal Win11 tray, no desktop Language Bar needed).
extern const GUID GUID_CantoSettingsButton;

// Icon index used when registering the profile (0 = default/no icon).
#define TEXTSERVICE_ICON_INDEX 0

// Registry root for our per-user settings (dictionary choice, candidate font).
// Shared across variants. The "Keyboard options" dialog writes here (see Settings).
#define CANTO_REG_SUBKEY    L"Software\\CantoIME"

// Stock dictionaries shipped next to the DLL. traditional.txt is the default for
// Traditional variants, simplified.txt for Simplified variants.
#define CANTO_DICT_FILENAME   L"traditional.txt"
#define CANTO_DICT_SIMPLIFIED L"simplified.txt"

// Default candidate window font cell height, in pixels.
#define CANTO_DEFAULT_FONT_SIZE 18

// Maximum candidates shown at once (number keys 1..9).
#define CANTO_MAX_CANDIDATES 9

// ---------------------------------------------------------------------------
// Process-wide state
// ---------------------------------------------------------------------------

// The DLL's module handle, captured in DllMain.
extern HINSTANCE g_hInst;

// Global COM/server reference count. When it reaches zero and no class
// factory locks are held, DllCanUnloadNow returns S_OK.
extern LONG g_cRefDll;

// Helpers to bump/drop the DLL reference count.
void DllAddRef();
void DllRelease();

// Returns the directory containing the loaded DLL (with trailing backslash).
std::wstring GetModuleDirectory();

// Resolves the dictionary path from the Dictionary setting (the sole selector):
// Stock Traditional -> traditional.txt, Stock Simplified -> simplified.txt (both
// next to the DLL), Custom -> the configured path. See Settings.
std::wstring ResolveDictionaryPath();
