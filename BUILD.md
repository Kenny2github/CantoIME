# Building and developing CantoIME

For installation, typing controls, and dictionary customization, see [README.md](README.md).

CantoIME is a Windows 11 TSF (Text Services Framework) text service implemented as
an in-process COM DLL.

## Language variants

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


## Repository layout

```
CantoIME/
├── CMakeLists.txt          # builds CantoIME-HK/-TW/-MO/-CN/-SG.dll (x64, C++17)
├── exports.def             # exports the 4 COM entry points
├── traditional.txt         # stock Traditional dictionary (installed next to the DLL)
├── simplified.txt          # stock Simplified dictionary (installed next to the DLL)
├── LICENSE                 # MIT
├── README.md
├── BUILD.md                # build and development instructions
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

After registering, add the keyboard in Windows Settings as described in
[Installation](README.md#installation).

## Installer upgrades

To update, just run a newer `CantoIME-Setup.exe`. There is **no need to
uninstall first**: the installer keeps a constant `AppId`, so Inno Setup detects
the existing install and upgrades it in place, overwriting the files and
re-registering the IME. If the DLL happens to be loaded by a running process at
the time, the swap is queued for the next reboot (the `restartreplace` flag), so
Windows may ask you to restart to finish the update. Bump `MyAppVersion` in
`installer/setup.iss` for each release so the upgrade is recorded.

---

## Checking the release workflow

Run the build-selection tests with Node.js:

```
node --test .github/tests/release-workflow.test.cjs
```

These cover exact-commit selection, pending builds, failed builds, and timeout.
They do not publish a release or upload assets.

## Publishing a release

1. Bump `MyAppVersion` in `installer/setup.iss` and push the commit.
2. Wait for the **Build** workflow to succeed for that commit.
3. Publish a GitHub release tagged to that commit. Publishing a draft release
   also triggers the release workflow.

The release workflow downloads the `CantoIME-x64` artifact from a successful
Build run for the tag's exact commit and uploads only `CantoIME-Setup.exe` to the
release. It waits up to ten minutes for a missing or pending build. It does not
substitute an installer from another commit. Re-running the release job replaces
an existing asset with the same name.

GitHub Actions artifacts expire according to the repository's retention policy.
If the artifact has expired or is missing, re-run Build for the tagged commit,
then re-run the release job.

## Implementation details

### Input indicator

The input indicator itself shows **粵** (Yue / Cantonese) when CantoIME is the
active input method, in place of the generic 繁體. The 中 / 英 mode icon follows
the light/dark theme live (it is drawn on demand, so we recolor it). The 粵
brand icon is a static Windows-8-style tile (a black glyph on a white box),
legible on both light and dark taskbars. It does **not** invert in dark mode
the way the built-in IMEs do: that inversion is applied by Microsoft's IME host
only to built-in/system IMEs, and a third-party TIP cannot opt into it
(confirmed by pointing CantoIME at a built-in IME's own icon; it still
rendered un-inverted).

---

### Per-user settings

The options dialog stores settings in `HKCU\Software\CantoIME`, with custom
candidates in `HKCU\Software\CantoIME\Overrides`. Overrides form an in-memory
overlay on top of the selected dictionary.

Windows Settings only exposes "Keyboard options" for Microsoft in-box IMEs.
CantoIME opens its dialog through a double-click on the 中/英 mode button.

The dictionary is parsed on a **background thread**, so switching to CantoIME (or
changing the dictionary/overrides) never blocks the UI. Typing produces no candidates until the load finishes, usually in a fraction
of a second.

### Dictionary segmentation

Multi-key input is segmented **greedily by longest key**: every way to split the
buffer into leading exact keys plus a prefix-matched final segment is enumerated,
then ordered by how much each key consumes, longest first. So if the dictionary
has entries for `abc`, `def`, `g`, `abcd`, and `efg`, typing `abcdefg` lists
`abcd efg` before `abc def g`, because `abcd` is longer than `abc`, not because
it uses fewer keys. (The whole-buffer prefix match consumes the most with its
first key, so a real single key spanning the buffer still ranks first.)

The stock dictionaries live next to the DLL. There is no `%APPDATA%` or registry
path search for them.

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

## How it works

- **TSF text service.** The DLL exposes a COM object (`CLSID_CantoIME`)
  implementing `ITfTextInputProcessorEx`. Windows' `msctf` loads it into the
  focused application's process and calls `ActivateEx`, where we advise our
  keyboard and thread-manager sinks and load the dictionary.
- **Key handling.** `ITfKeyEventSink::OnTestKeyDown` reports which keys we want
  to consume (without side effects); `OnKeyDown` does the real work: appending
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
  segmentations are ranked greedily by longest key consumed (see [Dictionary](README.md#dictionary-runtime-customizable)). Matches populate the candidate list, each carrying the Chinese
  text to commit plus the display string (which may include the final segment's
  leftover romanization).

---

### Composition styling

No display-attribute provider is registered; the composition uses the default
underline styling. The caret-position fallback (`GetCaretPos` -> `ClientToScreen`
on the foreground window) is heuristic.

## Installer note: WiX alternative

Instead of Inno Setup you can package with **WiX Toolset**: author a `.wxs` that
lays the DLL + `traditional.txt` / `simplified.txt` into a `ProgramFiles64Folder`
component and runs the DLL's self-registration. WiX can self-register via a `SelfReg` reference or,
better, by invoking 64-bit `regsvr32` from a deferred, elevated `CustomAction`
sequenced on install/uninstall, mirroring the `[Run]`/`[UninstallRun]` steps in
`installer/setup.iss`. WiX gives you an MSI (better for enterprise deployment)
at the cost of more verbose authoring.

---
