; setup.iss  -  Inno Setup script for the Minimal Pinyin IME (Sample)
;
; Builds MinimalPinyinIME-Setup.exe which:
;   * installs the DLL + dictionary to {autopf}\MinimalPinyinIME (64-bit),
;   * writes HKLM\Software\MinimalPinyinIME\DictionaryPath,
;   * registers the IME via 64-bit regsvr32 on install,
;   * unregisters it via regsvr32 /u on uninstall (before files are removed).
;
; Compile with:  iscc installer\setup.iss
; (Assumes the DLL was already built to ..\build\Release\MinimalPinyinIME.dll)

#define MyAppName "Minimal Pinyin IME (Sample)"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Sample"

[Setup]
AppId={{C1D2E3F4-A5B6-4C7D-8E9F-0A1B2C3D4E5F}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\MinimalPinyinIME
DisableProgramGroupPage=yes
OutputBaseFilename=MinimalPinyinIME-Setup
Compression=lzma
SolidCompression=yes
; 64-bit only.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern

[Files]
; Built DLL. Adjust the source path if your build output differs.
Source: "..\build\Release\MinimalPinyinIME.dll"; DestDir: "{app}"; Flags: ignoreversion 64bit
; Runtime-customizable dictionary.
Source: "..\dictionary.txt"; DestDir: "{app}"; Flags: ignoreversion

[Registry]
; Point the IME at the installed dictionary so it is found first in the search order.
Root: HKLM; Subkey: "Software\MinimalPinyinIME"; ValueType: string; \
    ValueName: "DictionaryPath"; ValueData: "{app}\dictionary.txt"; \
    Flags: uninsdeletekey

[Run]
; Register the TSF text service. Use the 64-bit regsvr32 explicitly via {sys}.
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\MinimalPinyinIME.dll"""; \
    StatusMsg: "Registering input method..."; Flags: runhidden waituntilterminated

[UninstallRun]
; Unregister BEFORE files are removed (RunBeforeFiles).
; [UninstallRun] entries execute before files are deleted, so the DLL still
; exists at unregistration time.
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\MinimalPinyinIME.dll"""; \
    Flags: runhidden waituntilterminated; RunOnceId: "UnregIME"
