// OptionsDialog.h
//
// The CantoIME options dialog, opened by double-clicking the 中/英 mode button.
// It edits the per-user CantoSettings.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// Shows the modal options dialog parented to hwndParent (may be nullptr) and
// saves changes to HKCU. Returns true if the user accepted (clicked OK).
bool ShowOptionsDialog(HWND hwndParent);
