# CantoIME

CantoIME is a Windows 11 Chinese input method with a customizable romanization-to-character dictionary. Type romanized text, then choose Chinese characters from the candidate list.

## Installation

Requires **Windows 11 x64**.

1. In **Settings > Time & language > Language & region**, select **Add a language**
   and add the Chinese language you want to use, if it is not already installed.
   CantoIME supports Traditional Chinese (Hong Kong SAR, Taiwan, and Macao SAR)
   and Simplified Chinese (China and Singapore).
2. Download `CantoIME-Setup.exe` from [Releases](https://github.com/Kenny2github/CantoIME/releases)
   and run it. Allow administrator access when prompted.
3. Choose one of your installed Chinese languages. The installer registers
   CantoIME under that language; it does **not** add it to your available keyboards.
4. Click **Open language settings** on the final installer page, or open
   **Settings > Time & language > Language & region**. Open **Language options**
   for the language you chose, then select **Add a keyboard > CantoIME**.
5. Switch to CantoIME with **Win + Space** or the taskbar input selector.
   Try typing in a text box, such as Notepad.

### Updating

Run the newer `CantoIME-Setup.exe`. You do not need to uninstall first.
Windows may ask you to restart if an application is using CantoIME during the update.

## What it does

- You type ASCII letters `a`-`z`. They accumulate in a **romanization buffer** shown
  as underlined text inside the target app.
- On each keystroke the buffer is looked up in the dictionary and matching
  candidate characters appear in a small **popup** near the caret,
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
- **Number keys `1`-`9`** accept the candidate in that row of the currently
  selected page (column).
- **Space** or **Enter** accepts the currently selected candidate (commits the
  raw buffer if there are no candidates).
- **Backspace** deletes the last letter (updates composition + candidates); if
  the buffer empties, the composition is cancelled. If not composing, it passes
  through.
- **Esc** cancels the composition and clears the buffer.
- **Any other key while composing** commits the current best result (first
  candidate if any, otherwise the raw buffer) and then lets that key pass
  through to the app.

### Chinese / English mode

Like Bopomofo and other Chinese IMEs, CantoIME has a **Chinese (中)** and an
**English (英)** mode, shown by an input-mode icon in the language bar next to
the input indicator. In Chinese mode letters build a composition as above; in
English mode letters pass straight through as plain English.

- **Press and release Shift** (with no other key in between) toggles the mode.
- **Clicking the 中 / 英 language-bar icon** toggles it too.
- Switching to English while composing commits the current candidate first.

The taskbar input indicator shows **粵** when CantoIME is active. The **中 / 英**
mode icon follows the light/dark theme. The 粵 icon stays black on white in both themes.

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

You can type several romanizations together. CantoIME tries longer matching keys
first: with keys `abc`, `def`, `g`, `abcd`, and `efg`, typing `abcdefg` lists
`abcd efg` before `abc def g`.

The installed stock dictionaries live in `C:\Program Files\CantoIME\`.
For bulk changes, choose **Custom** in Options and select your own dictionary file.

### Options

**Double-click the 中/英 mode button** in the taskbar input area to open the options
dialog. (A single click toggles Chinese/English; a double-click toggles twice, leaving
the mode unchanged, and opens settings.) This works in the normal Windows 11 tray with no
extra setup. Click **OK** to save your changes.

The dialog provides these settings:

- **Dictionary**: a dropdown that is the *sole* selector for which dictionary is
  read: *Stock Traditional Chinese dictionary* (`traditional.txt`), *Stock
  Simplified Chinese dictionary* (`simplified.txt`), or *Custom* (Browse for your
  own file). The default follows the variant's script: Traditional (HK/TW/MO)
  defaults to `traditional.txt`, Simplified (CN/SG) to `simplified.txt`.
- **Chinese/English toggle key**: which shift key, tapped on its own, switches
  between Chinese input and English passthrough: Both shifts (default), Left
  Shift, Right Shift, or Neither (disables the shift toggle; the 中/英 language-bar
  button still works). Holding shift for capitals is unaffected.
- **Candidate font size**: the candidate window font height; the window width
  grows with it. Default 18 px.
- **Custom candidates**: associate a romanization key with one or more extra
  candidates without editing any file. Type a key (e.g. `nei`) and one or more
  space-separated candidates (e.g. `你 妳`), then **Add / Update**; select a row
  to edit it, or **Remove** it. Extra candidates appear after the stock ones for that
  key. A key with no stock entry gets its own entry.

Changes apply the next time CantoIME gains focus (re-select it or click into a
text box). If the selected dictionary file is missing, no candidates are produced
(typing still works; Enter commits the raw romanization).

---

## Known limitations

- **No tone input** and no fuzzy/romanization correction.
- **Leading segments are locked to one character.** Every segmentation is
  enumerated and ranked greedily by longest key, but each leading segment is
  locked to its single best (first-listed) character; there is no per-segment
  candidate choice for the locked portion. Enumeration is capped for very long
  buffers.
- **Not DPI-aware**; the popup uses fixed pixel metrics.
- The candidate popup may appear in an imprecise position in some apps.

---

## Building from source

See [BUILD.md](BUILD.md) for build prerequisites, commands, manual registration,
and implementation details.

## License

MIT. See [`LICENSE`](LICENSE). Author: AbyxDev.
