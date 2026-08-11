// OptionsDialog.cpp

#include "OptionsDialog.h"
#include "Settings.h"
#include "Globals.h"
#include "resource.h"

#include <commdlg.h>   // GetOpenFileName
#include <algorithm>
#include <vector>
#include <string>

// Dictionary source dropdown. The selected index maps 1:1 to DictSource and is
// the sole selector for which dictionary the IME reads.
enum { DICT_TRAD = 0, DICT_SIMP = 1, DICT_CUSTOM = 2 };

static int SourceToIndex(DictSource s)
{
    switch (s)
    {
    case DictSource::StockSimplified: return DICT_SIMP;
    case DictSource::Custom:          return DICT_CUSTOM;
    default:                          return DICT_TRAD;
    }
}

static DictSource IndexToSource(LRESULT i)
{
    switch (i)
    {
    case DICT_SIMP:   return DictSource::StockSimplified;
    case DICT_CUSTOM: return DictSource::Custom;
    default:          return DictSource::StockTraditional;
    }
}

static void FillDictModeCombo(HWND hCombo)
{
    SendMessageW(hCombo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Stock Traditional Chinese dictionary"));
    SendMessageW(hCombo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Stock Simplified Chinese dictionary"));
    SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Custom"));
}

// Enables the path edit + Browse only when "Custom" is selected, and disables OK
// while "Custom" is selected without a file chosen.
static void UpdateDictControls(HWND hDlg)
{
    LRESULT mode = SendMessageW(GetDlgItem(hDlg, IDC_DICT_MODE), CB_GETCURSEL, 0, 0);
    BOOL custom = (mode == DICT_CUSTOM);
    EnableWindow(GetDlgItem(hDlg, IDC_DICT_PATH), custom);
    EnableWindow(GetDlgItem(hDlg, IDC_BROWSE), custom);

    BOOL okAllowed = !custom ||
        GetWindowTextLengthW(GetDlgItem(hDlg, IDC_DICT_PATH)) > 0;
    EnableWindow(GetDlgItem(hDlg, IDOK), okAllowed);
}

static void FillFontCombo(HWND hCombo, int current)
{
    std::vector<int> sizes = { 12, 14, 16, 18, 20, 22, 24, 28, 32, 36, 48 };
    if (std::find(sizes.begin(), sizes.end(), current) == sizes.end())
        sizes.push_back(current);
    std::sort(sizes.begin(), sizes.end());

    for (int sz : sizes)
    {
        std::wstring text = std::to_wstring(sz);
        int i = static_cast<int>(SendMessageW(hCombo, CB_ADDSTRING, 0,
                                              reinterpret_cast<LPARAM>(text.c_str())));
        SendMessageW(hCombo, CB_SETITEMDATA, i, static_cast<LPARAM>(sz));
        if (sz == current)
            SendMessageW(hCombo, CB_SETCURSEL, i, 0);
    }
}

static LRESULT ComboData(HWND hCombo)
{
    int i = static_cast<int>(SendMessageW(hCombo, CB_GETCURSEL, 0, 0));
    if (i == CB_ERR)
        return -1;
    return SendMessageW(hCombo, CB_GETITEMDATA, i, 0);
}

// Toggle-key dropdown. Item data holds the ToggleKeyMode value.
struct ToggleChoice { const wchar_t* label; ToggleKeyMode mode; };
static const ToggleChoice kToggleChoices[] = {
    { L"Both shifts (default)", ToggleKeyMode::Both },
    { L"Left Shift",            ToggleKeyMode::LeftShift },
    { L"Right Shift",           ToggleKeyMode::RightShift },
    { L"Neither",               ToggleKeyMode::Neither },
};

static void FillToggleCombo(HWND hCombo, ToggleKeyMode current)
{
    for (const ToggleChoice& c : kToggleChoices)
    {
        int i = static_cast<int>(SendMessageW(hCombo, CB_ADDSTRING, 0,
                                              reinterpret_cast<LPARAM>(c.label)));
        SendMessageW(hCombo, CB_SETITEMDATA, i, static_cast<LPARAM>(c.mode));
        if (c.mode == current)
            SendMessageW(hCombo, CB_SETCURSEL, i, 0);
    }
}

static void OnBrowse(HWND hDlg)
{
    wchar_t path[MAX_PATH] = { 0 };
    GetDlgItemTextW(hDlg, IDC_DICT_PATH, path, MAX_PATH);

    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner   = hDlg;
    ofn.lpstrFilter = L"Text files (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle  = L"Select dictionary file";

    if (GetOpenFileNameW(&ofn))
        SetDlgItemTextW(hDlg, IDC_DICT_PATH, path);
}

static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM /*lParam*/)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        CantoSettings s = LoadSettings();

        FillDictModeCombo(GetDlgItem(hDlg, IDC_DICT_MODE));
        SendMessageW(GetDlgItem(hDlg, IDC_DICT_MODE), CB_SETCURSEL,
                     SourceToIndex(s.dictSource), 0);
        SetDlgItemTextW(hDlg, IDC_DICT_PATH, s.dictionaryPath.c_str());
        UpdateDictControls(hDlg);

        FillToggleCombo(GetDlgItem(hDlg, IDC_TOGGLE), s.toggleKey);
        FillFontCombo(GetDlgItem(hDlg, IDC_FONTSIZE), s.fontSize);
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_DICT_MODE:
            if (HIWORD(wParam) == CBN_SELCHANGE)
                UpdateDictControls(hDlg);
            return TRUE;

        case IDC_DICT_PATH:
            // Re-check the OK button as the custom path is typed/cleared/browsed.
            if (HIWORD(wParam) == EN_CHANGE)
                UpdateDictControls(hDlg);
            return TRUE;

        case IDC_BROWSE:
            OnBrowse(hDlg);
            return TRUE;

        case IDOK:
        {
            LRESULT mode = SendMessageW(GetDlgItem(hDlg, IDC_DICT_MODE), CB_GETCURSEL, 0, 0);
            // Custom requires a file; refuse to submit without one (the OK button
            // is also disabled, but guard here in case Enter reaches us).
            if (mode == DICT_CUSTOM &&
                GetWindowTextLengthW(GetDlgItem(hDlg, IDC_DICT_PATH)) == 0)
                return TRUE;

            // Start from the stored settings so a remembered custom path survives
            // switching to a stock choice and back.
            CantoSettings s = LoadSettings();

            s.dictSource = IndexToSource(mode);
            if (s.dictSource == DictSource::Custom)
            {
                wchar_t path[MAX_PATH] = { 0 };
                GetDlgItemTextW(hDlg, IDC_DICT_PATH, path, MAX_PATH);
                s.dictionaryPath = path;
            }

            LRESULT toggle = ComboData(GetDlgItem(hDlg, IDC_TOGGLE));
            if (toggle >= 0)
                s.toggleKey = static_cast<ToggleKeyMode>(toggle);

            LRESULT font = ComboData(GetDlgItem(hDlg, IDC_FONTSIZE));
            if (font > 0)
                s.fontSize = static_cast<int>(font);

            SaveSettings(s);
            EndDialog(hDlg, IDOK);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        return FALSE;

    default:
        return FALSE;
    }
}

bool ShowOptionsDialog(HWND hwndParent)
{
    INT_PTR r = DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_OPTIONS),
                                hwndParent, DlgProc, 0);
    return r == IDOK;
}
