; setup.iss  -  Inno Setup script for CantoIME
;
; Builds CantoIME-Setup.exe which lets the user pick any combination of the
; Traditional Chinese variants Windows offers and, for each chosen variant:
;   * installs its DLL to {autopf}\CantoIME (64-bit),
;   * registers the TSF text service via 64-bit regsvr32 on install,
;   * unregisters it via regsvr32 /u on uninstall (before files are removed).
; The shared dictionary and HKLM\Software\CantoIME\DictionaryPath are always
; installed. "Chinese (Traditional, Hong Kong SAR)" is the sole default.
;
; Compile with:  iscc installer\setup.iss
; (Assumes the DLLs were already built to ..\build\Release\CantoIME-*.dll)

#define MyAppName "CantoIME"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "CantoIME"

[Setup]
AppId={{C1D2E3F4-A5B6-4C7D-8E9F-0A1B2C3D4E5F}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\CantoIME
DisableProgramGroupPage=yes
OutputBaseFilename=CantoIME-Setup
Compression=lzma
SolidCompression=yes
; 64-bit only.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern

[Types]
; "Default" installs only Hong Kong SAR. "Full" installs all three. "Custom"
; lets the user tick any combination.
Name: "default"; Description: "Default (Hong Kong SAR only)"
Name: "full"; Description: "All Traditional Chinese variants"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
; Hong Kong SAR is in the "default" type, so it is the sole default selection.
Name: "hk"; Description: "Chinese (Traditional, Hong Kong SAR)"; Types: default full custom
Name: "tw"; Description: "Chinese (Traditional, Taiwan)"; Types: full
Name: "mo"; Description: "Chinese (Traditional, Macao SAR)"; Types: full

[Files]
; One DLL per selected variant.
Source: "..\build\Release\CantoIME-HK.dll"; DestDir: "{app}"; Components: hk; Flags: ignoreversion 64bit
Source: "..\build\Release\CantoIME-TW.dll"; DestDir: "{app}"; Components: tw; Flags: ignoreversion 64bit
Source: "..\build\Release\CantoIME-MO.dll"; DestDir: "{app}"; Components: mo; Flags: ignoreversion 64bit
; Shared, runtime-customizable dictionary (always installed).
Source: "..\dictionary.txt"; DestDir: "{app}"; Flags: ignoreversion

[Registry]
; Point every variant at the installed dictionary (shared key).
Root: HKLM; Subkey: "Software\CantoIME"; ValueType: string; \
    ValueName: "DictionaryPath"; ValueData: "{app}\dictionary.txt"; \
    Flags: uninsdeletekey

[Run]
; Register each selected variant's TSF text service via the 64-bit regsvr32.
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\CantoIME-HK.dll"""; \
    Components: hk; StatusMsg: "Registering Hong Kong SAR input method..."; \
    Flags: runhidden waituntilterminated
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\CantoIME-TW.dll"""; \
    Components: tw; StatusMsg: "Registering Taiwan input method..."; \
    Flags: runhidden waituntilterminated
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\CantoIME-MO.dll"""; \
    Components: mo; StatusMsg: "Registering Macao SAR input method..."; \
    Flags: runhidden waituntilterminated

[UninstallRun]
; Unregister BEFORE files are removed. [UninstallRun] entries execute before
; files are deleted, so each DLL still exists at unregistration time. The
; Components filter means only variants that were installed get unregistered.
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\CantoIME-HK.dll"""; \
    Components: hk; Flags: runhidden waituntilterminated; RunOnceId: "UnregIME_HK"
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\CantoIME-TW.dll"""; \
    Components: tw; Flags: runhidden waituntilterminated; RunOnceId: "UnregIME_TW"
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\CantoIME-MO.dll"""; \
    Components: mo; Flags: runhidden waituntilterminated; RunOnceId: "UnregIME_MO"
