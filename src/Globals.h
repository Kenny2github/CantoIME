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

// Exactly one CANTO_VARIANT_* must be defined. Default to Hong Kong SAR when
// the build did not specify one (e.g. a plain single-target compile).
#if !defined(CANTO_VARIANT_HK) && !defined(CANTO_VARIANT_TW) && !defined(CANTO_VARIANT_MO)
#  define CANTO_VARIANT_HK
#endif

#if defined(CANTO_VARIANT_HK)
#  define TEXTSERVICE_LANGID  MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_HONGKONG)    // 0x0C04
#  define TEXTSERVICE_DESC    L"CantoIME (Chinese, Traditional, Hong Kong SAR)"
#  define CANTO_VARIANT_TAG   L"HK"
#elif defined(CANTO_VARIANT_TW)
#  define TEXTSERVICE_LANGID  MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL) // 0x0404
#  define TEXTSERVICE_DESC    L"CantoIME (Chinese, Traditional, Taiwan)"
#  define CANTO_VARIANT_TAG   L"TW"
#elif defined(CANTO_VARIANT_MO)
#  define TEXTSERVICE_LANGID  MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_MACAU)       // 0x1404
#  define TEXTSERVICE_DESC    L"CantoIME (Chinese, Traditional, Macao SAR)"
#  define CANTO_VARIANT_TAG   L"MO"
#endif

// ---------------------------------------------------------------------------
// Text service identity
// ---------------------------------------------------------------------------

// CLSID of the text service COM object. Distinct per variant (see Globals.cpp).
extern const CLSID CLSID_CantoIME;

// Language profile GUID. Each (LANGID, GUID_Profile) pair is a distinct
// input method entry that Windows shows in the input switcher. Distinct per
// variant (see Globals.cpp).
extern const GUID GUID_Profile;

// Icon index used when registering the profile (0 = default/no icon).
#define TEXTSERVICE_ICON_INDEX 0

// Registry root for our own settings (dictionary path override). Shared across
// variants: they all read the same dictionary.
#define CANTO_REG_SUBKEY    L"Software\\CantoIME"
#define CANTO_REG_DICTVALUE L"DictionaryPath"

// Default dictionary file name shipped next to the DLL.
#define CANTO_DICT_FILENAME L"dictionary.txt"

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

// Returns the full resolved dictionary path using the documented search order.
std::wstring ResolveDictionaryPath();
