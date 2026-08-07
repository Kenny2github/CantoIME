# Minimal Pinyin IME (Sample)

A minimal, buildable Windows 11 **TSF (Text Services Framework)** text service
that behaves like Microsoft Bopomofo in HanYu Pinyin mode, but with a
**customizable pinyin -> character dictionary loaded at runtime** from a plain
text file. It is an in-proc COM DLL (`MinimalPinyinIME.dll`) registered under
LANGID `0x0404` (zh-TW), so it shows up alongside Chinese (Traditional) input
methods.

> **Honest caveat:** this repository was authored on **macOS** and has **NOT
> been compiled or tested on Windows**. It targets Windows 11 x64 and cannot be
> built on macOS/Linux (it needs the MSVC toolchain + Windows SDK). Expect to
> iterate on minor build errors (missing SDK symbols, link flags, header
> ordering). The code follows the documented TSF COM interfaces and the general
> "SampleIME" architecture, but it is original code, not a copy of Microsoft's
> sample.

---

## What it does

- You type ASCII letters `a`–`z`. They accumulate in a **pinyin buffer** shown
  as an underlined TSF composition inside the target app.
- On each keystroke the buffer is looked up in the dictionary and matching
  candidate characters appear in a small **owner-drawn popup** near the caret,
  listed as `1 拼  2 品  3 聘 …` (up to 9).
- **Number keys `1`–`9`** select the corresponding candidate and commit it.
- **Space** selects the first candidate (commits raw buffer if there are none).
- **Enter** commits the raw typed pinyin text as-is.
- **Backspace** deletes the last letter (updates composition + candidates); if
  the buffer empties, the composition is cancelled. If not composing, it passes
  through.
- **Esc** cancels the composition and clears the buffer.
- **Any other key while composing** commits the current best result (first
  candidate if any, otherwise the raw buffer) and then lets that key pass
  through to the app. (This is the documented, simplest-coherent choice.)

---

## Dictionary (runtime customizable)

`dictionary.txt` is plain **UTF-8** (a leading BOM is tolerated). One entry per
line:

```
pinyin<TAB or spaces>char1 char2 char3 ...
```

Candidates are space-separated. Lines beginning with `#` are comments. Matching
is exact and case-insensitive; there is no tone input.

### Dictionary search order (documented)

The DLL resolves the dictionary path at activation time, in this order:

1. **Registry override** — REG_SZ value
   `Software\MinimalPinyinIME\DictionaryPath`, checked first under **HKCU**,
   then **HKLM**. (The installer writes the HKLM value.)
2. **`dictionary.txt` next to the DLL.**
3. **`%APPDATA%\MinimalPinyinIME\dictionary.txt`.**

The first existing file wins. If none exist, no candidates are produced (typing
still works; Enter commits the raw pinyin).

To customize at runtime: edit the file the registry points at (or drop your own
`dictionary.txt` next to the DLL), then re-select the IME (or sign out/in) so it
reloads on the next activation.

---

## Repository layout

```
pinyin-ime-sample/
├── CMakeLists.txt          # builds MinimalPinyinIME.dll (x64, C++17, Unicode)
├── exports.def             # exports the 4 COM entry points
├── dictionary.txt          # sample dictionary (~35 syllables)
├── LICENSE                 # MIT
├── README.md
├── installer/
│   └── setup.iss           # Inno Setup script -> MinimalPinyinIME-Setup.exe
└── src/
    ├── Globals.h/.cpp      # CLSID/GUIDs, LANGID, hInst, DLL refcount, path logic
    ├── dllmain.cpp         # DllMain, class factory, DllGetClassObject,
    │                       #   DllCanUnloadNow, DllRegister/UnregisterServer
    ├── TextService.h/.cpp  # ITfTextInputProcessorEx + key/thread/composition sinks
    ├── EditSession.h       # reusable ITfEditSession adapter (lambda callback)
    ├── CandidateWindow.h/.cpp  # owner-drawn GDI popup, CJK font
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
git clone <your-fork-url> pinyin-ime-sample
cd pinyin-ime-sample
```

### 2. Build the DLL with CMake

From a "**x64 Native Tools Command Prompt for VS 2022**" (or any shell with
CMake on PATH):

```
cmake -B build -A x64
cmake --build build --config Release
```

This produces `build\Release\MinimalPinyinIME.dll` and copies `dictionary.txt`
next to it.

#### Alternative: build directly with `cl`

From the x64 Native Tools prompt, in the repo root:

```
cl /nologo /LD /EHsc /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN ^
   src\Globals.cpp src\Dictionary.cpp src\CandidateWindow.cpp ^
   src\TextService.cpp src\dllmain.cpp ^
   /link /DEF:exports.def /OUT:MinimalPinyinIME.dll ^
   user32.lib gdi32.lib ole32.lib oleaut32.lib advapi32.lib uuid.lib shell32.lib imm32.lib
```

### 3. Build the installer (optional)

```
iscc installer\setup.iss
```

Produces `installer\Output\MinimalPinyinIME-Setup.exe`. (The script expects the
DLL at `build\Release\MinimalPinyinIME.dll`; adjust the `[Files]` source path if
you built elsewhere.)

### 4. Install (as administrator)

Run `MinimalPinyinIME-Setup.exe`. It installs the DLL + dictionary to
`C:\Program Files\MinimalPinyinIME\`, writes the registry `DictionaryPath`
value, and registers the IME with 64-bit `regsvr32 /s`.

To register/unregister manually instead of using the installer, from an
**elevated** prompt:

```
regsvr32 MinimalPinyinIME.dll        :: register
regsvr32 /u MinimalPinyinIME.dll     :: unregister
```

Use the 64-bit `regsvr32` (`C:\Windows\System32\regsvr32.exe`) for the 64-bit
DLL.

### 5. Enable / switch to the IME

The profile registers under **Chinese (Traditional) — zh-TW (0x0404)**.

1. Open **Settings > Time & language > Language & region**.
2. If **Chinese (Traditional)** is not listed, click **Add a language** and add
   it.
3. Sign out and back in (or restart) so Windows picks up the newly registered
   text service.
4. Switch input methods with **Win + Space** (or the language button on the
   taskbar) until **"Minimal Pinyin IME (Sample)"** is selected.
5. Type in any edit control (Notepad, WordPad, the search box) to see the
   composition + candidate window.

### 6. Customize the dictionary at runtime

Edit the installed `dictionary.txt` (its path is the registry value
`HKLM\Software\MinimalPinyinIME\DictionaryPath`, i.e.
`C:\Program Files\MinimalPinyinIME\dictionary.txt`). The file is re-read the
next time the IME is activated, so re-select the IME (or sign out/in). You can
also edit `dictionary.txt` in the repo before building/installing.

---

## How it works

- **TSF text service.** The DLL exposes a COM object (`CLSID_MinimalPinyinIME`)
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
- **Dictionary lookup.** The buffer is looked up exactly (case-insensitively) on
  every keystroke; matches populate the candidate list.

---

## Known limitations (this is a sample)

- **No tone input** and no fuzzy/pinyin correction.
- **Exact-key lookup only** — no longest-prefix segmentation of multi-syllable
  input; each buffer is matched as a single key.
- **Single-character commit** — candidates are single Han characters; no phrase
  candidates and **no user phrase learning**.
- **Candidate paging is limited to the first 9** matches; no page up/down.
- **Not DPI-aware**; the popup uses fixed pixel metrics.
- The caret-position fallback (`GetCaretPos` -> `ClientToScreen` on the
  foreground window) is heuristic and may be imprecise in some apps.
- No display-attribute provider is registered; the composition uses the default
  underline styling.

---

## Installer note: WiX alternative

Instead of Inno Setup you can package with **WiX Toolset**: author a `.wxs` that
lays the DLL + `dictionary.txt` into a `ProgramFiles64Folder` component, writes
the `HKLM\Software\MinimalPinyinIME\DictionaryPath` registry value, and runs the
DLL's self-registration. WiX can self-register via a `SelfReg` reference or,
better, by invoking 64-bit `regsvr32` from a deferred, elevated `CustomAction`
sequenced on install/uninstall — mirroring the `[Run]`/`[UninstallRun]` steps in
`installer/setup.iss`. WiX gives you an MSI (better for enterprise deployment)
at the cost of more verbose authoring.

---

## License

MIT — see [`LICENSE`](LICENSE). Author: "Sample".
