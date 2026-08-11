// resource.h
//
// Control and dialog identifiers for the CantoIME "Keyboard options" dialog
// (see CantoIME.rc and OptionsDialog.cpp).

#pragma once

#define IDD_OPTIONS   101

#define IDC_DICT_MODE 1000
#define IDC_DICT_PATH 1001
#define IDC_BROWSE    1002
#define IDC_TOGGLE    1003
#define IDC_FONTSIZE  1004

// Custom-candidate override controls.
#define IDC_OVR_LIST   1005
#define IDC_OVR_KEY    1006
#define IDC_OVR_CANDS  1007
#define IDC_OVR_ADD    1008
#define IDC_OVR_REMOVE 1009

#ifndef IDC_STATIC
#define IDC_STATIC    (-1)
#endif
