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

// Enables the path edit + Browse only when "Custom" is selected.
static void UpdateDictControls(HWND hDlg)
{
    LRESULT mode = SendMessageW(GetDlgItem(hDlg, IDC_DICT_MODE), CB_GETCURSEL, 0, 0);
    BOOL custom = (mode == DICT_CUSTOM);
    EnableWindow(GetDlgItem(hDlg, IDC_DICT_PATH), custom);
    EnableWindow(GetDlgItem(hDlg, IDC_BROWSE), custom);
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

        case IDC_BROWSE:
            OnBrowse(hDlg);
            return TRUE;

        case IDOK:
        {
            // Start from the stored settings so a remembered custom path survives
            // switching to a stock choice and back.
            CantoSettings s = LoadSettings();

            LRESULT mode = SendMessageW(GetDlgItem(hDlg, IDC_DICT_MODE), CB_GETCURSEL, 0, 0);
            s.dictSource = IndexToSource(mode);
            if (s.dictSource == DictSource::Custom)
            {
                wchar_t path[MAX_PATH] = { 0 };
                GetDlgItemTextW(hDlg, IDC_DICT_PATH, path, MAX_PATH);
                s.dictionaryPath = path;
            }

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
