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

## Typing

Type romanized text to see matching Chinese characters. Partial spellings also
show suggestions.

- **Space / Enter** accepts the selected candidate; **1-9** or a click chooses one
  directly. With no candidates, Space / Enter inserts the text you typed.
- **Up / Down** moves through candidates; **PgUp / PgDn** changes pages. You can
  also scroll over the candidate list.
- **Right** expands the list into columns; **Left / Right** moves between them.
- **Backspace** removes a letter; **Esc** cancels the current input.
- Tap **Shift**, or click **中 / 英** in the taskbar, to switch between Chinese
  and English input.

## Options

Double-click **中 / 英** in the taskbar to open Options. You can:

- Choose the stock Traditional or Simplified dictionary, or a custom file.
- Choose which Shift key toggles Chinese/English, or disable the shortcut.
- Change the candidate font size.
- Add or remove custom candidates for a spelling. These additions are saved
  separately for each dictionary and appear after its stock candidates.

Click **OK** to apply your changes. Other open applications pick them up when
CantoIME regains focus.
For bulk dictionary changes, see [Custom dictionary files](BUILD.md#custom-dictionary-files).

## Limitations

- No tone input or spelling correction.
- When typing several syllables together, only the final segment offers a choice
  of candidates; earlier segments use their first match.
- Candidate sizing and positioning may not suit every display or application.

## Building from source

See [BUILD.md](BUILD.md) for build instructions and implementation details.

## License

MIT. See [LICENSE](LICENSE). Author: AbyxDev.
