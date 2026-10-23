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
- **Git with Git LFS** (the stock dictionaries are LFS files).
- **Visual Studio 2022** with the **"Desktop development with C++"** workload and
  the **Windows 11 SDK** (provides `msctf.h`, `ctffunc.h`, `uuid.lib`, etc.).
- **CMake 3.20+** (bundled with VS 2022, or standalone).
- **Inno Setup 6** (for building the installer): <https://jrsoftware.org/isdl.php>.

---

## Build steps

### 1. Clone

```
git lfs install
git clone <your-fork-url> CantoIME
cd CantoIME
git lfs pull
```

### 2. Build the DLLs and tools with CMake

From a "**x64 Native Tools Command Prompt for VS 2022**" (or any shell with
CMake on PATH):

```
cmake -B build -A x64
cmake --build build --config Release
```

This produces all five variant DLLs (`build\Release\CantoIME-HK/-TW/-MO/-CN/-SG.dll`)
plus `canto-mkcache.exe` and `canto-dicttest.exe`, and copies the two stock
dictionaries next to them. The CMake build links the MSVC runtime statically,
so these binaries do not require a separate Visual C++ redistributable.

Run the dictionary cache checks used by CI:

```
build\Release\canto-dicttest.exe traditional.txt simplified.txt
```

They compare cached lookups with text parsing and check oversized entries,
stale caches, and malformed caches. The **Build** workflow runs these checks
and compiles the installer on Windows for pushes and manual dispatches.

#### Alternative: build directly with `cl`

From the x64 Native Tools prompt, in the repo root. Pick the variant with the
matching `CANTO_VARIANT_*` define and output name (here: Hong Kong SAR):

```
rc /nologo /I src /fo CantoIME.res src\CantoIME.rc
cl /nologo /LD /MT /EHsc /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN ^
   /DNOMINMAX /DCANTO_VARIANT_HK ^
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
build links it in. This command builds only a DLL, not the cache builder or
installer inputs. Use CMake for the installer build.

### 3. Build the installer (optional)

```
iscc installer\setup.iss
```

Produces `installer\Output\CantoIME-Setup.exe`. (The script expects all five DLLs
and `canto-mkcache.exe` in `build\Release\`; adjust the `[Files]` source paths if you built
elsewhere.) The wizard builds its variant list from the Chinese languages you have
installed and lets you pick **one** (Hong Kong SAR is the default when present);
if you have no Chinese language, it shows an "add a language first" page instead.

### 4. Install (as administrator)

Add the matching Chinese language in Windows **first** (the installer requires
it). Run `CantoIME-Setup.exe`, choose the variant, and it installs that DLL,
the two stock dictionaries and `canto-mkcache.exe` to
`C:\Program Files\CantoIME\`. It prebuilds both stock dictionary caches and registers
the chosen variant with 64-bit `regsvr32 /s`. Per-user options (dictionary choice,
font size, Shift toggle key) are written to `HKCU\Software\CantoIME` later, by the options dialog.

To register/unregister manually instead of using the installer, from an
**elevated** prompt (use the DLL(s) for the variant(s) you want):

```
rem Register Hong Kong SAR
regsvr32 CantoIME-HK.dll
rem Unregister Hong Kong SAR
regsvr32 /u CantoIME-HK.dll
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

CMake generates the 粵 profile icon with `tools/make-canto-icon.ps1` and links
its `.res` into each DLL. `LangBarButton.cpp` draws the separate 中/英 mode
icon using the taskbar theme setting. The profile icon is a static resource.

### Per-user settings

The options dialog stores settings in `HKCU\Software\CantoIME`, with custom
candidates in `HKCU\Software\CantoIME\Overrides\<dictId>`. Stock dictionaries
have fixed identities; custom dictionaries use a hash of their lowercased, trimmed path.
Each dictionary has its own override set. Overrides append candidates without
changing the source file and form an in-memory overlay on the loaded dictionary.

Double-click the 中/英 mode button to open the options dialog. The text service
also implements `ITfFnConfigure::Show`. Saving through the mode-button dialog
applies settings immediately in that process; other instances re-read settings
when gaining focus.

### Dictionary loading

`CTextService::BeginLoadDictionary` still starts a background thread. Normally,
`Dictionary::Load` maps a prebuilt cache rather than parsing the text dictionary.
The installer builds the stock caches; selecting a new custom dictionary in
Options launches `canto-mkcache.exe` as a detached, best-effort process.

If no usable cache exists, the loader parses the text and attempts to cache it.
The worker posts the loaded dictionary back to the service's message window,
where overrides are applied. On initial activation, candidates are unavailable
until loading finishes. During a reload, the previous dictionary remains in use
until the result arrives. If the service has no message window, loading falls
back to a synchronous call.

### Custom dictionary files

Use UTF-8 text, with an optional leading BOM, and one key per line:

```text
romanization<TAB or spaces>candidate1 candidate2 candidate3
```

Keys and candidates are separated by spaces or tabs. Blank lines and lines whose
first non-whitespace character is `#` are ignored. Keys are case-insensitive;
a later line with the same key replaces the earlier candidate list. Duplicate
candidates within a line are removed, preserving their first occurrence.

The final segment uses prefix matching: with `gao` mapped to 高, typing `ga`
displays `高o`, including the untyped suffix. Leading segments must match exact
keys. Select **Custom** in Options to use your file; the text service accepts
alphabetic romanization input, not tone digits.

### Dictionary segmentation

Multi-key input is segmented **greedily by longest key**: the code enumerates
splits into leading exact keys plus a prefix-matched final segment,
then ordered by how much each key consumes, longest first. So if the dictionary
has entries for `abc`, `def`, `g`, `abcd`, and `efg`, typing `abcdefg` lists
`abcd efg` before `abc def g`, because `abcd` is longer than `abc`, not because
it uses fewer keys. The search is capped at 256 splits, 200 prefix keys per
final segment, and 300 candidates. (The whole-buffer prefix match consumes the most with its
first key, so a real single key spanning the buffer still ranks first.)

The stock dictionaries live next to the DLL. There is no `%APPDATA%` or registry
path search for them.

### Dictionary cache (mmap)

Parsing the text dictionary is the slow part of a load, so each dictionary also
gets a binary cache laid out for memory mapping. The IME maps it read-only and
answers lookups in place: a binary search over a small page index picks the one
data block that holds a key (normally one 4 KB page; oversized entries use
multiple pages). Prefix scans can visit several blocks. Layout (see `src/Dictionary.cpp`):

- **Header page:** magic, format version, the source file's size + last-write
  time (a mismatch means stale), entry/block/page counts, region offsets.
- **Page index:** one entry per data block (first page, page count, first key),
  page-aligned.
- **Data blocks:** sorted entries packed into 4 KB pages, each block with its
  own entry count and offset table. An entry too big for one page gets a block
  spanning a run of whole pages to itself.

Stock dictionaries are cached next to the file (`traditional.txt.cache`) by the
installer via `canto-mkcache.exe`. Custom dictionaries are cached per user at
`%LOCALAPPDATA%\CantoIME\cache\<hash>.cache` (prebuilt when locked in through
the options dialog, without waiting for the helper to finish). Stock loads also
try the per-user cache if no usable sibling cache exists. The loader validates
the header and index on mapping and bounds-checks entries on access. If mapping
fails, it parses the text and attempts to write a replacement cache. Stock cache
writes fall back to the per-user directory if the installation directory is not
writable. If caching fails, lookups continue using the parsed dictionary.
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
- **Dictionary lookup.** On every keystroke the buffer is segmented into
  leading exact keys plus a prefix-matched final segment, subject to the search
  caps above. The
  segmentations are ranked greedily by longest key consumed (see [Dictionary segmentation](#dictionary-segmentation)). Matches populate the candidate list, each carrying the Chinese
  text to commit plus the display string (which may include the final segment's
  leftover romanization).

---

### Composition styling

No display-attribute provider is registered; the composition uses the default
underline styling. The caret-position fallback (`GetCaretPos` -> `ClientToScreen`
on the foreground window) is heuristic.
