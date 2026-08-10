# CantoIME

A minimal, buildable Windows 11 **TSF (Text Services Framework)** text service
that behaves like Microsoft Bopomofo, but with a **customizable romanization ->
character dictionary loaded at runtime** from a plain text file. It is an in-proc
COM DLL that registers as a Traditional Chinese input method.

CantoIME ships **one DLL per Traditional Chinese variant Windows offers**, so
you can enable it under whichever locale(s) you use:

| DLL | Windows locale | LANGID |
| --- | --- | --- |
| `CantoIME-HK.dll` | Chinese (Traditional, Hong Kong SAR) | `0x0C04` |
| `CantoIME-TW.dll` | Chinese (Traditional, Taiwan) | `0x0404` |
| `CantoIME-MO.dll` | Chinese (Traditional, Macao SAR) | `0x1404` |

The three are built from the same source; each is compiled with a
`CANTO_VARIANT_*` define that selects its LANGID, description, CLSID, profile
GUID, and window class, and each registers as an independent input method so
they can be registered independently. Because all three share one dictionary,
the installer treats them as mutually exclusive: you pick **one** variant to
install, with **Hong Kong SAR** as the default. (You can still register the
other DLLs by hand if you want more than one.)

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
brand icon is a static grayscale tile in the same format as the built-in IMEs'
icons, but it does **not** invert in dark mode: that inversion is applied by
Microsoft's IME host only to built-in/system IMEs, and a third-party TIP cannot
opt into it (confirmed by pointing CantoIME at a built-in IME's own icon — it
still renders un-inverted). So the brand icon is theme-neutral and readable on
both light and dark taskbars.

---

## Dictionary (runtime customizable)

`dictionary.txt` is plain **UTF-8** (a leading BOM is tolerated). One entry per
line:

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

### Dictionary search order (documented)

The DLL resolves the dictionary path at activation time, in this order:

1. **Registry override** — REG_SZ value
   `Software\CantoIME\DictionaryPath`, checked first under **HKCU**,
   then **HKLM**. (The installer writes the HKLM value.)
2. **`dictionary.txt` next to the DLL.**
3. **`%APPDATA%\CantoIME\dictionary.txt`.**

The first existing file wins. If none exist, no candidates are produced (typing
still works; Enter commits the raw romanization).

To customize at runtime: edit the file the registry points at (or drop your own
`dictionary.txt` next to the DLL), then re-select the IME (or sign out/in) so it
reloads on the next activation.

---

## Repository layout

```
CantoIME/
├── CMakeLists.txt          # builds CantoIME-HK/-TW/-MO.dll (x64, C++17, Unicode)
├── exports.def             # exports the 4 COM entry points
├── dictionary.txt          # sample dictionary (~35 syllables)
├── LICENSE                 # MIT
├── README.md
├── installer/
│   └── setup.iss           # Inno Setup script -> CantoIME-Setup.exe
└── src/
    ├── Globals.h/.cpp      # CLSID/GUIDs, LANGID, hInst, DLL refcount, path logic
    ├── dllmain.cpp         # DllMain, class factory, DllGetClassObject,
    │                       #   DllCanUnloadNow, DllRegister/UnregisterServer
    ├── TextService.h/.cpp  # ITfTextInputProcessorEx + key/thread/composition sinks
    ├── EditSession.h       # reusable ITfEditSession adapter (lambda callback)
    ├── CandidateWindow.h/.cpp  # owner-drawn GDI popup, CJK font
    ├── LangBarButton.h/.cpp    # language-bar buttons (粵 icon, 中/英 mode toggle)
    └── Dictionary.h/.cpp   # UTF-8 loader + lookup + reload
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

This produces all three variant DLLs (`build\Release\CantoIME-HK.dll`,
`CantoIME-TW.dll`, `CantoIME-MO.dll`) and copies `dictionary.txt` next to them.

#### Alternative: build directly with `cl`

From the x64 Native Tools prompt, in the repo root. Pick the variant with the
matching `CANTO_VARIANT_*` define and output name (here: Hong Kong SAR):

```
cl /nologo /LD /EHsc /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN ^
   /DCANTO_VARIANT_HK ^
   src\Globals.cpp src\Dictionary.cpp src\CandidateWindow.cpp ^
   src\TextService.cpp src\dllmain.cpp ^
   /link /DEF:exports.def /OUT:CantoIME-HK.dll ^
   user32.lib gdi32.lib ole32.lib oleaut32.lib advapi32.lib uuid.lib shell32.lib imm32.lib
```

Repeat with `/DCANTO_VARIANT_TW` / `/OUT:CantoIME-TW.dll` and
`/DCANTO_VARIANT_MO` / `/OUT:CantoIME-MO.dll` for the other variants.

### 3. Build the installer (optional)

```
iscc installer\setup.iss
```

Produces `installer\Output\CantoIME-Setup.exe`. (The script expects the DLLs at
`build\Release\CantoIME-*.dll`; adjust the `[Files]` source paths if you built
elsewhere.) The wizard shows the three variants as a radio group (mutually
exclusive, since they share one dictionary), with **Hong Kong SAR** selected by
default.

### 4. Install (as administrator)

Run `CantoIME-Setup.exe`. Choose which variant to install, then it installs that
DLL + the shared dictionary to `C:\Program Files\CantoIME\`, writes the registry
`DictionaryPath` value, and registers the chosen variant with
64-bit `regsvr32 /s`.

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

### 6. Customize the dictionary at runtime

Edit the installed `dictionary.txt` (its path is the registry value
`HKLM\Software\CantoIME\DictionaryPath`, i.e.
`C:\Program Files\CantoIME\dictionary.txt`). The file is re-read the
next time the IME is activated, so re-select the IME (or sign out/in). You can
also edit `dictionary.txt` in the repo before building/installing.

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
lays the DLL + `dictionary.txt` into a `ProgramFiles64Folder` component, writes
the `HKLM\Software\CantoIME\DictionaryPath` registry value, and runs the
DLL's self-registration. WiX can self-register via a `SelfReg` reference or,
better, by invoking 64-bit `regsvr32` from a deferred, elevated `CustomAction`
sequenced on install/uninstall — mirroring the `[Run]`/`[UninstallRun]` steps in
`installer/setup.iss`. WiX gives you an MSI (better for enterprise deployment)
at the cost of more verbose authoring.

---

## License

MIT — see [`LICENSE`](LICENSE). Author: AbyxDev.
