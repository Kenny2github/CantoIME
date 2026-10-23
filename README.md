# CantoIME

A minimal, buildable Windows 11 **TSF (Text Services Framework)** text service
that behaves like Microsoft Bopomofo, but with a **customizable romanization ->
character dictionary loaded at runtime** from a plain text file. It is an in-proc
COM DLL that registers as a Chinese input method.

CantoIME ships **one DLL per Chinese variant Windows offers**, so you can enable
it under whichever locale(s) you use:

| DLL | Windows locale | LANGID |
| --- | --- | --- |
| `CantoIME-HK.dll` | Chinese (Traditional, Hong Kong SAR) | `0x0C04` |
| `CantoIME-TW.dll` | Chinese (Traditional, Taiwan) | `0x0404` |
| `CantoIME-MO.dll` | Chinese (Traditional, Macao SAR) | `0x1404` |
| `CantoIME-CN.dll` | Chinese (Simplified, China) | `0x0804` |
| `CantoIME-SG.dll` | Chinese (Simplified, Singapore) | `0x1004` |

They are built from the same source; each is compiled with a `CANTO_VARIANT_*`
define that selects its LANGID, CLSID, profile GUID, and window class, and each
registers as an independent input method. The installer builds its variant list
from the Chinese languages you have installed and lets you pick **one** (Hong
Kong SAR is the default when present). (You can still register the other DLLs by
hand if you want more than one.)

---

## What it does

- You type ASCII letters `a`–`z`. They accumulate in a **romanization buffer** shown
  as an underlined TSF composition inside the target app.
- On each keystroke the buffer is looked up in the dictionary and matching
  candidate characters appear in a small **owner-drawn popup** near the caret,
  listed as `1 拼  2 品  3 聘 …` (9 per page). The first candidate is selected by
  default (highlighted).
- **Click** a candidate to accept it.
- **Arrow keys** move the selection. In the default vertical mode, pressing
  **Down** past the 9th candidate reveals the next page (selecting its 1st),
  and **Up** past the 1st reveals the previous page. **Mouse wheel** ticks act
  as Up/Down while the cursor is over the candidate list; elsewhere the wheel
  scrolls the underlying app as usual.
- **PgDn / PgUp** move a whole page and reset the selection to the top of it.
- **Right arrow** latches a two-dimensional layout for the rest of the
  composition: further 9-candidate pages appear as columns to the right, and
  **Left / Right** move between them. Only as many columns as fit the screen
  width are shown at once; moving past the last visible column scrolls the grid
  rather than drawing off screen.
- **Number keys `1`–`9`** accept the candidate in that row of the currently
  selected page (column).
- **Space** or **Enter** accepts the currently selected candidate (commits the
  raw buffer if there are no candidates).
- **Backspace** deletes the last letter (updates composition + candidates); if
  the buffer empties, the composition is cancelled. If not composing, it passes
  through.
- **Esc** cancels the composition and clears the buffer.
- **Any other key while composing** commits the current best result (first
  candidate if any, otherwise the raw buffer) and then lets that key pass
  through to the app. (This is the documented, simplest-coherent choice.)

### Chinese / English mode

Like Bopomofo and other Chinese IMEs, CantoIME has a **Chinese (中)** and an
**English (英)** mode, shown by an input-mode icon in the language bar next to
the input indicator. In Chinese mode letters build a composition as above; in
English mode letters pass straight through as plain English.

- **Press and release Shift** (with no other key in between) toggles the mode.
- **Clicking the 中 / 英 language-bar icon** toggles it too.
- Switching to English while composing commits the current candidate first.

The input indicator itself shows **粵** (Yue / Cantonese) when CantoIME is the
active input method, in place of the generic 繁體. The 中 / 英 mode icon follows
the light/dark theme live (it is drawn on demand, so we recolor it). The 粵
brand icon is a static Windows-8-style tile — a black glyph on a white box —
legible on both light and dark taskbars. It does **not** invert in dark mode
the way the built-in IMEs do: that inversion is applied by Microsoft's IME host
only to built-in/system IMEs, and a third-party TIP cannot opt into it
(confirmed by pointing CantoIME at a built-in IME's own icon — it still
rendered un-inverted).

---

## Dictionary (runtime customizable)

The stock dictionaries `traditional.txt` and `simplified.txt` are plain **UTF-8**
(a leading BOM is tolerated). One entry per line:

```
romanization<TAB or spaces>char1 char2 char3 ...
```

Candidates are space-separated. Lines beginning with `#` are comments. Matching
is case-insensitive; there is no tone input. The final (still-open) segment is
matched by **prefix**: typing `ga` surfaces every key beginning with `ga`, and a
prefix-only match shows its leftover romanization after the character (e.g. with
`gao` → 高, typing `ga` displays `高o`). Any leading segments in a multi-segment
buffer must be **exact** keys.

Multi-key input is segmented **greedily by longest key**: every way to split the
buffer into leading exact keys plus a prefix-matched final segment is enumerated,
then ordered by how much each key consumes, longest first. So if the dictionary
has entries for `abc`, `def`, `g`, `abcd`, and `efg`, typing `abcdefg` lists
`abcd efg` before `abc def g` — because `abcd` is longer than `abc`, not because
it uses fewer keys. (The whole-buffer prefix match consumes the most with its
first key, so a real single key spanning the buffer still ranks first.)

The stock dictionaries `traditional.txt` and `simplified.txt` live next to the
DLL; no `%APPDATA%` and no registry path search are used.

### Options

**Double-click the 中/英 mode button** in the taskbar input area to open the options
dialog. (A single click toggles Chinese/English; a double-click toggles twice — net
no change — and opens settings.) This works in the normal Windows 11 tray with no
extra setup. Clicking **OK** applies the changes immediately. (Windows' Settings →
"Keyboard options" is Microsoft-in-box-IME-only, so a third-party TIP cannot expose
config there; the double-click is the entry point.)

The dialog edits three per-user settings, stored in `HKCU\Software\CantoIME`:

- **Dictionary** — a dropdown that is the *sole* selector for which dictionary is
  read: *Stock Traditional Chinese dictionary* (`traditional.txt`), *Stock
  Simplified Chinese dictionary* (`simplified.txt`), or *Custom* (Browse for your
  own file). The default follows the variant's script: Traditional (HK/TW/MO)
  defaults to `traditional.txt`, Simplified (CN/SG) to `simplified.txt`.
- **Chinese/English toggle key** — which shift key, tapped on its own, switches
  between Chinese input and English passthrough: Both shifts (default), Left
  Shift, Right Shift, or Neither (disables the shift toggle; the 中/英 language-bar
  button still works). Holding shift for capitals is unaffected.
- **Candidate font size** — the candidate window font height; the window width
  grows with it. Default 18 px.
- **Custom candidates** — associate a romanization key with one or more extra
  candidates without editing any file. Type a key (e.g. `nei`) and one or more
  space-separated candidates (e.g. `你 妳`), then **Add / Update**; select a row
  to edit it, or **Remove** it. Overrides are stored under
  `HKCU\Software\CantoIME\Overrides` and are **merged on top of** the selected
  dictionary at load: the extra candidates are appended after the stock ones for
  that key (a key with no stock entry is added outright). This replaces the old
  advice to hand-edit a `.txt` file for small personalizations.

Changes apply the next time CantoIME gains focus (re-select it or click into a
text box). If the selected dictionary file is missing, no candidates are produced
(typing still works; Enter commits the raw romanization).

The dictionary is parsed on a **background thread**, so switching to CantoIME (or
changing the dictionary/overrides) never blocks the UI. Until the load finishes
— a fraction of a second — typing simply produces no candidates yet.

### Dictionary cache (mmap)

Parsing the text dictionary is the slow part of a load, so each dictionary also
gets a binary cache laid out for memory mapping. The IME maps it read-only and
answers lookups in place: a binary search over a small page index picks the one
4 KB page that holds a key, and only the pages actually touched are read from
disk. Layout (see `src/Dictionary.cpp`):

- **Header page:** magic, format version, the source file's size + last-write
  time (a mismatch means stale), entry/block/page counts, region offsets.
- **Page index:** one entry per data block (first page, page count, first key),
  page-aligned.
- **Data pages:** sorted entries packed into 4 KB pages, never straddling a
  page, each page with its own entry count and offset table. An entry too big
  for one page gets a run of whole pages to itself.

Stock dictionaries are cached next to the file (`traditional.txt.cache`) by the
installer via `canto-mkcache.exe`. Custom dictionaries are cached per user at
`%LOCALAPPDATA%\CantoIME\cache\<hash>.cache` (prebuilt when locked in through
the options dialog). A missing, stale, or damaged cache is never trusted: the
IME falls back to parsing the text and rewrites the cache for next time.
Overrides are kept in a small in-memory overlay consulted on top of the mapping.

---

## Repository layout

```
CantoIME/
├── CMakeLists.txt          # builds CantoIME-HK/-TW/-MO/-CN/-SG.dll (x64, C++17)
├── exports.def             # exports the 4 COM entry points
├── traditional.txt         # stock Traditional dictionary (installed next to the DLL)
├── simplified.txt          # stock Simplified dictionary (installed next to the DLL)
├── LICENSE                 # MIT
├── README.md
├── installer/
│   └── setup.iss           # Inno Setup script -> CantoIME-Setup.exe
├── tools/
│   └── make-canto-icon.ps1 # generates the 粵 indicator icon (.res) at build time
└── src/
    ├── Globals.h/.cpp      # CLSID/GUIDs, LANGID, hInst, DLL refcount, dict resolution
    ├── dllmain.cpp         # DllMain, class factory, DllRegister/UnregisterServer
    ├── TextService.h/.cpp  # TIP sinks + ITfFnConfigure (options dialog)
    ├── EditSession.h       # reusable ITfEditSession adapter (lambda callback)
    ├── CandidateWindow.h/.cpp  # owner-drawn GDI popup, sizable CJK font
    ├── LangBarButton.h/.cpp    # language-bar buttons (粵 icon, 中/英 mode toggle)
    ├── Dictionary.h/.cpp   # UTF-8 loader, mmap cache, lookup
    ├── mkcache.cpp         # canto-mkcache.exe: prebuilds dictionary caches
    ├── dicttest.cpp        # canto-dicttest.exe: cache check run in CI
    ├── Settings.h/.cpp     # per-user options in HKCU\Software\CantoIME
    ├── OptionsDialog.h/.cpp # "Keyboard options" dialog (ITfFnConfigure::Show)
    ├── resource.h          # dialog/control IDs
    └── CantoIME.rc         # options dialog resource (icon is a separate .res)
```

---

## Prerequisites

- **Windows 11 x64**
- **Visual Studio 2022** with the **"Desktop development with C++"** workload and
  the **Windows 11 SDK** (provides `msctf.h`, `ctffunc.h`, `uuid.lib`, etc.).
- **CMake 3.20+** (bundled with VS 2022, or standalone).
- **Inno Setup 6** (for building the installer): <https://jrsoftware.org/isdl.php>.

---

## Build steps

### 1. Clone

```
git clone <your-fork-url> CantoIME
cd CantoIME
```

### 2. Build the DLL with CMake

From a "**x64 Native Tools Command Prompt for VS 2022**" (or any shell with
CMake on PATH):

```
cmake -B build -A x64
cmake --build build --config Release
```

This produces all five variant DLLs (`build\Release\CantoIME-HK/-TW/-MO/-CN/-SG.dll`)
and copies `traditional.txt` + `simplified.txt` next to them.

#### Alternative: build directly with `cl`

From the x64 Native Tools prompt, in the repo root. Pick the variant with the
matching `CANTO_VARIANT_*` define and output name (here: Hong Kong SAR):

```
rc /nologo /fo CantoIME.res src\CantoIME.rc
cl /nologo /LD /EHsc /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN ^
   /DCANTO_VARIANT_HK ^
   src\Globals.cpp src\Dictionary.cpp src\CandidateWindow.cpp ^
   src\TextService.cpp src\LangBarButton.cpp src\Settings.cpp ^
   src\OptionsDialog.cpp src\dllmain.cpp CantoIME.res ^
   /link /DEF:exports.def /OUT:CantoIME-HK.dll ^
   user32.lib gdi32.lib ole32.lib oleaut32.lib advapi32.lib uuid.lib ^
   shell32.lib comdlg32.lib imm32.lib
```

Repeat with `/DCANTO_VARIANT_TW`, `/DCANTO_VARIANT_MO`, `/DCANTO_VARIANT_CN`, and
`/DCANTO_VARIANT_SG` (with matching `/OUT:`). This `cl` path omits the 粵 indicator
icon (a separate generated `.res`, see `tools/make-canto-icon.ps1`); the CMake
build links it in. The DLL is fully functional without it.

### 3. Build the installer (optional)

```
iscc installer\setup.iss
```

Produces `installer\Output\CantoIME-Setup.exe`. (The script expects all five DLLs
at `build\Release\CantoIME-*.dll`; adjust the `[Files]` source paths if you built
elsewhere.) The wizard builds its variant list from the Chinese languages you have
installed and lets you pick **one** (Hong Kong SAR is the default when present);
if you have no Chinese language, it shows an "add a language first" page instead.

### 4. Install (as administrator)

Add the matching Chinese language in Windows **first** (the installer requires
it). Run `CantoIME-Setup.exe`, choose the variant, and it installs that DLL +
`traditional.txt` / `simplified.txt` to `C:\Program Files\CantoIME\` and registers
the chosen variant with 64-bit `regsvr32 /s`. Per-user options (dictionary choice,
font size) are written to `HKCU\Software\CantoIME` later, by the options dialog.

To register/unregister manually instead of using the installer, from an
**elevated** prompt (use the DLL(s) for the variant(s) you want):

```
regsvr32 CantoIME-HK.dll        :: register Hong Kong SAR
regsvr32 /u CantoIME-HK.dll     :: unregister Hong Kong SAR
```

Use the 64-bit `regsvr32` (`C:\Windows\System32\regsvr32.exe`) for the 64-bit
DLLs.

### 5. Enable / switch to the IME

Each variant registers under its matching locale: **Hong Kong SAR (0x0C04)**,
**Taiwan (0x0404)**, or **Macao SAR (0x1404)** — all under Chinese (Traditional).

Once the DLL is registered, the IME appears in the input switcher on its own:
you do **not** have to add the Chinese (Traditional) language pack for it to
show up, so if this is the only Chinese keyboard you plan to use, you can skip
the "Add a language" step entirely.

If you *do* want the matching locale added (e.g. you also use other Chinese
input methods and want them grouped under it), it is best to add the locale
**before** installing this IME, so Windows slots the new keyboard under it
cleanly.

1. (Optional) In **Settings > Time & language > Language & region**, click
   **Add a language** and add the **Chinese (Traditional)** locale for your
   variant — do this *before* installing if you want it.
2. Sign out and back in (or restart) so Windows picks up the newly registered
   text service.
3. Switch input methods with **Win + Space** (or the language button on the
   taskbar) until the **"CantoIME (…)"** entry for your variant is selected.
4. Type in any edit control (Notepad, WordPad, the search box) to see the
   composition + candidate window.

### 6. Customize at runtime

Open **Keyboard options** (see [Options](#options-keyboard-options-dialog)) to
choose the dictionary (Stock Traditional / Stock Simplified / Custom), the
candidate font size, and to add **custom candidates** for individual keys via the
UI (no file editing needed). To change many entries at once you can still edit the
installed `traditional.txt` / `simplified.txt` in `C:\Program Files\CantoIME\`
directly, or point *Custom* at your own file. Changes are re-read the next time
CantoIME gains focus (re-select it or click into a text box). You can also edit
the stock files in the repo before building/installing.

### Updating

To update, just run a newer `CantoIME-Setup.exe`. There is **no need to
uninstall first**: the installer keeps a constant `AppId`, so Inno Setup detects
the existing install and upgrades it in place, overwriting the files and
re-registering the IME. If the DLL happens to be loaded by a running process at
the time, the swap is queued for the next reboot (the `restartreplace` flag), so
Windows may ask you to restart to finish the update. Bump `MyAppVersion` in
`installer/setup.iss` for each release so the upgrade is recorded.

---

## How it works

- **TSF text service.** The DLL exposes a COM object (`CLSID_CantoIME`)
  implementing `ITfTextInputProcessorEx`. Windows' `msctf` loads it into the
  focused application's process and calls `ActivateEx`, where we advise our
  keyboard and thread-manager sinks and load the dictionary.
- **Key handling.** `ITfKeyEventSink::OnTestKeyDown` reports which keys we want
  to consume (without side effects); `OnKeyDown` does the real work — appending
  to the buffer, driving the composition, and committing.
- **Composition.** Document mutation must happen inside an `ITfEditSession`
  granted a valid edit cookie. `EditSession.h` is a small reusable adapter that
  runs a lambda inside the cookie. We start a composition with
  `ITfContextComposition::StartComposition` around a range obtained via
  `ITfInsertAtSelection`, update its text with `ITfRange::SetText`, and finish
  with `ITfComposition::EndComposition` on commit/cancel.
- **Candidate window.** A `WS_POPUP` tool-window that never takes focus, painted
  with GDI (`DrawTextW`) using the CJK font "Microsoft JhengHei". It is
  positioned via `ITfContextView::GetTextExt` on the composition range (run
  inside a read edit session), with a caret/cursor-position fallback.
- **Dictionary lookup.** On every keystroke the buffer is segmented every way it
  can be: any run of leading exact keys plus a prefix-matched final segment. The
  segmentations are ranked greedily by longest key consumed (see the dictionary
  format section). Matches populate the candidate list, each carrying the Chinese
  text to commit plus the display string (which may include the final segment's
  leftover romanization).

---

## Known limitations (this is a sample)

- **No tone input** and no fuzzy/romanization correction.
- **Leading segments are locked to one character.** Every segmentation is
  enumerated and ranked greedily by longest key, but each leading segment is
  locked to its single best (first-listed) character; there is no per-segment
  candidate choice for the locked portion. Enumeration is capped for very long
  buffers.
- **Not DPI-aware**; the popup uses fixed pixel metrics.
- The caret-position fallback (`GetCaretPos` -> `ClientToScreen` on the
  foreground window) is heuristic and may be imprecise in some apps.
- No display-attribute provider is registered; the composition uses the default
  underline styling.

---

## Installer note: WiX alternative

Instead of Inno Setup you can package with **WiX Toolset**: author a `.wxs` that
lays the DLL + `traditional.txt` / `simplified.txt` into a `ProgramFiles64Folder`
component and runs the DLL's self-registration. WiX can self-register via a `SelfReg` reference or,
better, by invoking 64-bit `regsvr32` from a deferred, elevated `CustomAction`
sequenced on install/uninstall — mirroring the `[Run]`/`[UninstallRun]` steps in
`installer/setup.iss`. WiX gives you an MSI (better for enterprise deployment)
at the cost of more verbose authoring.

---

## License

MIT — see [`LICENSE`](LICENSE). Author: AbyxDev.
