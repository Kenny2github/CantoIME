// Globals.h
//
// Shared globals, GUIDs, and constants for the Minimal Pinyin IME sample.
//
// This is a Windows 11 TSF (Text Services Framework) text service. It is an
// in-proc COM DLL. All the GUIDs below MUST be unique to this text service.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <msctf.h>   // TSF core interfaces
#include <string>

// ---------------------------------------------------------------------------
// Text service identity
// ---------------------------------------------------------------------------

// CLSID of the text service COM object.
// {A3D4E1F2-5B6C-4D7E-8F90-1A2B3C4D5E6F}
extern const CLSID CLSID_MinimalPinyinIME;

// Language profile GUID. Each (LANGID, GUID_Profile) pair is a distinct
// input method entry that Windows shows in the input switcher.
// {B4E5F2A3-6C7D-4E8F-9A01-2B3C4D5E6F70}
extern const GUID GUID_Profile;

// LANGID for zh-TW (Chinese, Traditional, Taiwan). Mirrors MS Bopomofo.
#define TEXTSERVICE_LANGID  MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL) // 0x0404

// Human-readable description shown in the Windows input switcher.
#define TEXTSERVICE_DESC    L"Minimal Pinyin IME (Sample)"

// Icon index used when registering the profile (0 = default/no icon).
#define TEXTSERVICE_ICON_INDEX 0

// Registry root for our own settings (dictionary path override).
#define MPIME_REG_SUBKEY    L"Software\\MinimalPinyinIME"
#define MPIME_REG_DICTVALUE L"DictionaryPath"

// Default dictionary file name shipped next to the DLL.
#define MPIME_DICT_FILENAME L"dictionary.txt"

// Maximum candidates shown at once (number keys 1..9).
#define MPIME_MAX_CANDIDATES 9

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
