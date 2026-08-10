// OptionsDialog.h
//
// The "Keyboard options" configuration dialog, shown from Windows language
// settings via ITfFnConfigure::Show. It edits the per-user CantoSettings.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// Shows the modal options dialog parented to hwndParent (may be nullptr) and
// saves changes to HKCU. Returns true if the user accepted (clicked OK).
bool ShowOptionsDialog(HWND hwndParent);
