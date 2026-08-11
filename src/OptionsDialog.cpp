// OptionsDialog.cpp

#include "OptionsDialog.h"
#include "Settings.h"
#include "Globals.h"
#include "resource.h"

#include <commdlg.h>   // GetOpenFileName
#include <algorithm>
#include <vector>
#include <string>
#include <cwctype>

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

// --- Custom-candidate overrides ----------------------------------------------
//
// The dialog edits a working copy of the overrides (a heap vector held in
// DWLP_USER) so Cancel discards changes. OK persists it via SaveOverrides.

static std::wstring LowerTrim(const std::wstring& s)
{
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) ++a;
    while (b > a && iswspace(s[b - 1])) --b;
    std::wstring out = s.substr(a, b - a);
    for (auto& c : out)
        c = static_cast<wchar_t>(towlower(c));
    return out;
}

static std::vector<std::wstring> SplitTokens(const std::wstring& s)
{
    std::vector<std::wstring> out;
    size_t i = 0;
    while (i < s.size())
    {
        while (i < s.size() && iswspace(s[i])) ++i;
        size_t start = i;
        while (i < s.size() && !iswspace(s[i])) ++i;
        if (i > start)
            out.push_back(s.substr(start, i - start));
    }
    return out;
}

static std::wstring JoinTokens(const std::vector<std::wstring>& v)
{
    std::wstring out;
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (i) out += L' ';
        out += v[i];
    }
    return out;
}

static std::vector<CantoOverride>* Model(HWND hDlg)
{
    return reinterpret_cast<std::vector<CantoOverride>*>(GetWindowLongPtrW(hDlg, DWLP_USER));
}

// Repopulates the list box from the model. The list order mirrors the model
// order (index N in the list is model[N]), so selection maps back directly.
static void RefreshOverrideList(HWND hDlg, int selectIndex)
{
    HWND hList = GetDlgItem(hDlg, IDC_OVR_LIST);
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);
    for (const CantoOverride& ov : *Model(hDlg))
    {
        std::wstring line = ov.first + L"\t" + JoinTokens(ov.second);
        SendMessageW(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
    if (selectIndex >= 0)
        SendMessageW(hList, LB_SETCURSEL, selectIndex, 0);
}

static void OnOverrideSelChange(HWND hDlg)
{
    LRESULT sel = SendMessageW(GetDlgItem(hDlg, IDC_OVR_LIST), LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR)
        return;
    const std::vector<CantoOverride>& model = *Model(hDlg);
    if (sel < 0 || static_cast<size_t>(sel) >= model.size())
        return;
    SetDlgItemTextW(hDlg, IDC_OVR_KEY, model[sel].first.c_str());
    SetDlgItemTextW(hDlg, IDC_OVR_CANDS, JoinTokens(model[sel].second).c_str());
}

static void OnOverrideAdd(HWND hDlg)
{
    wchar_t keyBuf[128] = { 0 };
    wchar_t candBuf[512] = { 0 };
    GetDlgItemTextW(hDlg, IDC_OVR_KEY, keyBuf, 128);
    GetDlgItemTextW(hDlg, IDC_OVR_CANDS, candBuf, 512);

    std::wstring key = LowerTrim(keyBuf);
    std::vector<std::wstring> cands = SplitTokens(candBuf);
    if (key.empty() || cands.empty())
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    std::vector<CantoOverride>& model = *Model(hDlg);
    auto it = std::find_if(model.begin(), model.end(),
                           [&](const CantoOverride& o) { return o.first == key; });
    if (it != model.end())
        it->second = std::move(cands); // update in place
    else
        model.emplace_back(key, std::move(cands));

    // Clear the fields and drop the selection so the next Add starts a fresh
    // entry rather than being stuck updating the one just committed. (Selecting
    // the added row would refill the fields and turn the next Add into an edit.)
    RefreshOverrideList(hDlg, -1);
    SetDlgItemTextW(hDlg, IDC_OVR_KEY, L"");
    SetDlgItemTextW(hDlg, IDC_OVR_CANDS, L"");
    SetFocus(GetDlgItem(hDlg, IDC_OVR_KEY));
}

static void OnOverrideRemove(HWND hDlg)
{
    LRESULT sel = SendMessageW(GetDlgItem(hDlg, IDC_OVR_LIST), LB_GETCURSEL, 0, 0);
    std::vector<CantoOverride>& model = *Model(hDlg);
    if (sel == LB_ERR || sel < 0 || static_cast<size_t>(sel) >= model.size())
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    model.erase(model.begin() + sel);
    SetDlgItemTextW(hDlg, IDC_OVR_KEY, L"");
    SetDlgItemTextW(hDlg, IDC_OVR_CANDS, L"");

    int next = static_cast<int>(sel);
    if (next >= static_cast<int>(model.size()))
        next = static_cast<int>(model.size()) - 1;
    RefreshOverrideList(hDlg, next);
}

// --- Resizable-layout support ------------------------------------------------
// Each control tracks the dialog's growth (dx, dy in pixels) per its anchor
// flags. Captured at WM_INITDIALOG, applied on WM_SIZE.
enum { ANCHOR_MOVEX = 1, ANCHOR_MOVEY = 2, ANCHOR_SIZEW = 4, ANCHOR_SIZEH = 8 };

struct AnchoredCtl { int id; unsigned flags; RECT rc; };

struct DlgLayout
{
    SIZE initClient;                 // client size at init
    SIZE minTrack;                   // minimum window size
    std::vector<AnchoredCtl> ctls;
};

static void InitLayout(HWND hDlg)
{
    static const struct { int id; unsigned flags; } table[] = {
        { IDC_DICT_MODE,     ANCHOR_SIZEW },
        { IDC_DICT_PATH,     ANCHOR_SIZEW },
        { IDC_BROWSE,        ANCHOR_MOVEX },
        { IDC_OVR_GROUP,     ANCHOR_SIZEW | ANCHOR_SIZEH },
        { IDC_OVR_LIST,      ANCHOR_SIZEW | ANCHOR_SIZEH },
        { IDC_OVR_KEYLABEL,  ANCHOR_MOVEY },
        { IDC_OVR_KEY,       ANCHOR_MOVEY },
        { IDC_OVR_CANDLABEL, ANCHOR_MOVEY },
        { IDC_OVR_CANDS,     ANCHOR_MOVEY | ANCHOR_SIZEW },
        { IDC_OVR_ADD,       ANCHOR_MOVEX | ANCHOR_MOVEY },
        { IDC_OVR_REMOVE,    ANCHOR_MOVEX | ANCHOR_MOVEY },
        { IDOK,              ANCHOR_MOVEX | ANCHOR_MOVEY },
        { IDCANCEL,          ANCHOR_MOVEX | ANCHOR_MOVEY },
    };

    DlgLayout* L = new DlgLayout();
    RECT rc; GetClientRect(hDlg, &rc);
    L->initClient.cx = rc.right;
    L->initClient.cy = rc.bottom;

    int listH = 0;
    for (const auto& t : table)
    {
        HWND h = GetDlgItem(hDlg, t.id);
        if (!h) continue;
        AnchoredCtl c{ t.id, t.flags, {} };
        GetWindowRect(h, &c.rc);
        MapWindowPoints(nullptr, hDlg, reinterpret_cast<POINT*>(&c.rc), 2);
        if (t.id == IDC_OVR_LIST) listH = c.rc.bottom - c.rc.top;
        L->ctls.push_back(c);
    }

    // Minimum window size. Width floors at the design width (labels would clip
    // if narrower). The dialog is "too tall" on small screens, so allow the
    // height to shrink until the list is ~3 rows, and no further.
    RECT wr; GetWindowRect(hDlg, &wr);
    L->minTrack.cx = wr.right - wr.left;
    int shrink = listH > 60 ? listH - 60 : 0;
    L->minTrack.cy = (wr.bottom - wr.top) - shrink;

    SetPropW(hDlg, L"CantoDlgLayout", reinterpret_cast<HANDLE>(L));
}

static void ApplyLayout(HWND hDlg)
{
    DlgLayout* L = reinterpret_cast<DlgLayout*>(GetPropW(hDlg, L"CantoDlgLayout"));
    if (!L) return;

    RECT rc; GetClientRect(hDlg, &rc);
    int dx = rc.right - L->initClient.cx;
    int dy = rc.bottom - L->initClient.cy;

    HDWP hdwp = BeginDeferWindowPos(static_cast<int>(L->ctls.size()));
    for (const auto& c : L->ctls)
    {
        HWND h = GetDlgItem(hDlg, c.id);
        if (!h) continue;
        int x = c.rc.left + ((c.flags & ANCHOR_MOVEX) ? dx : 0);
        int y = c.rc.top  + ((c.flags & ANCHOR_MOVEY) ? dy : 0);
        int w = (c.rc.right - c.rc.left) + ((c.flags & ANCHOR_SIZEW) ? dx : 0);
        int hh = (c.rc.bottom - c.rc.top) + ((c.flags & ANCHOR_SIZEH) ? dy : 0);
        if (hdwp)
            hdwp = DeferWindowPos(hdwp, h, nullptr, x, y, w, hh,
                                  SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (hdwp)
        EndDeferWindowPos(hdwp);

    // The groupbox frame smears when resized; repaint the dialog cleanly.
    InvalidateRect(hDlg, nullptr, TRUE);
}

static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
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

        // Working copy of the overrides; freed in WM_DESTROY.
        SetWindowLongPtrW(hDlg, DWLP_USER,
            reinterpret_cast<LONG_PTR>(new std::vector<CantoOverride>(LoadOverrides())));
        RefreshOverrideList(hDlg, -1);
        InitLayout(hDlg);
        return TRUE;
    }

    case WM_SIZE:
        ApplyLayout(hDlg);
        return TRUE;

    case WM_GETMINMAXINFO:
    {
        DlgLayout* L =
            reinterpret_cast<DlgLayout*>(GetPropW(hDlg, L"CantoDlgLayout"));
        if (L)
        {
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = L->minTrack.cx;
            mmi->ptMinTrackSize.y = L->minTrack.cy;
        }
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_OVR_LIST:
            if (HIWORD(wParam) == LBN_SELCHANGE)
                OnOverrideSelChange(hDlg);
            return TRUE;

        case IDC_OVR_ADD:
            OnOverrideAdd(hDlg);
            return TRUE;

        case IDC_OVR_REMOVE:
            OnOverrideRemove(hDlg);
            return TRUE;

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
            if (std::vector<CantoOverride>* model = Model(hDlg))
                SaveOverrides(*model);
            EndDialog(hDlg, IDOK);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        return FALSE;

    case WM_DESTROY:
        delete Model(hDlg);
        SetWindowLongPtrW(hDlg, DWLP_USER, 0);
        delete reinterpret_cast<DlgLayout*>(GetPropW(hDlg, L"CantoDlgLayout"));
        RemovePropW(hDlg, L"CantoDlgLayout");
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
