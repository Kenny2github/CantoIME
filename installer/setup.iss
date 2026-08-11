; setup.iss  -  Inno Setup script for CantoIME
;
; Builds CantoIME-Setup.exe. It offers ONE Chinese variant to install for (only
; the languages the user has already added in Windows are listed), and for the
; chosen variant:
;   * installs its DLL to {autopf}\CantoIME (64-bit),
;   * installs the stock dictionaries (traditional.txt, simplified.txt) alongside,
;   * registers the TSF text service via 64-bit regsvr32 on install,
;   * unregisters it via regsvr32 /u on uninstall (before files are removed).
;
; Variants: HK/TW/MO (Traditional), CN/SG (Simplified). The variant list on the
; selection page is built from the user's installed languages; if none is a
; Chinese language, that page is replaced by an "add a language first" message
; and installation is blocked. This avoids registering a TSF profile for a LANGID
; the user does not have (which shows up as a broken, mis-languaged keyboard) and
; is used instead of installing a language pack from Setup (that needs internet
; and usually a reboot, and cannot be done reliably here).
;
; Options (dictionary choice, toggle key, candidate font size) live per-user in
; HKCU\Software\CantoIME, edited via the IME's "Keyboard options" dialog and
; removed on uninstall. No %APPDATA% is used.
;
; Note: registering a TSF text service writes to HKLM, so admin is required (this
; is inherent to Windows IMEs; per-user/no-admin registration is not possible).
;
; Compile with:  iscc installer\setup.iss
; (Assumes the DLLs were already built to ..\build\Release\CantoIME-*.dll)

#define MyAppName "CantoIME"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "AbyxDev"

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

[Files]
; The DLL for the selected variant (exactly one is chosen on the variant page).
Source: "..\build\Release\CantoIME-HK.dll"; DestDir: "{app}"; Check: IsVariant('HK'); Flags: ignoreversion 64bit restartreplace uninsrestartdelete
Source: "..\build\Release\CantoIME-TW.dll"; DestDir: "{app}"; Check: IsVariant('TW'); Flags: ignoreversion 64bit restartreplace uninsrestartdelete
Source: "..\build\Release\CantoIME-MO.dll"; DestDir: "{app}"; Check: IsVariant('MO'); Flags: ignoreversion 64bit restartreplace uninsrestartdelete
Source: "..\build\Release\CantoIME-CN.dll"; DestDir: "{app}"; Check: IsVariant('CN'); Flags: ignoreversion 64bit restartreplace uninsrestartdelete
Source: "..\build\Release\CantoIME-SG.dll"; DestDir: "{app}"; Check: IsVariant('SG'); Flags: ignoreversion 64bit restartreplace uninsrestartdelete
; Stock dictionaries live next to the DLL. traditional.txt is the default the IME
; reads; simplified.txt ships for future use. Removed with the app on uninstall.
Source: "..\traditional.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\simplified.txt";  DestDir: "{app}"; Flags: ignoreversion
; The dictionary service. A per-user resident process that loads the dictionary
; once and answers the IME's lookup requests over a named pipe, so switching into
; the IME does not re-parse the dictionary. Removed with the app on uninstall.
Source: "..\build\Release\canto-dictd.exe"; DestDir: "{app}"; Flags: ignoreversion 64bit restartreplace uninsrestartdelete

[Registry]
; Clean up the per-user options key on uninstall (not created at install time;
; the IME writes it when the user changes options).
Root: HKCU; Subkey: "Software\CantoIME"; Flags: uninsdeletekey dontcreatekey
; Start the dictionary service at every user's logon, running as that user. It
; starts early (before the first switch into the IME) but self-gates: it stays
; and loads the dictionary only if that user has actually added a CantoIME
; keyboard, and exits immediately otherwise.
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; \
    ValueName: "CantoIME Dictionary Service"; ValueData: """{app}\canto-dictd.exe"""; \
    Flags: uninsdeletevalue

[Run]
; Register the selected variant's TSF text service via the 64-bit regsvr32.
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\CantoIME-HK.dll"""; \
    Check: IsVariant('HK'); StatusMsg: "Registering Hong Kong SAR input method..."; \
    Flags: runhidden waituntilterminated
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\CantoIME-TW.dll"""; \
    Check: IsVariant('TW'); StatusMsg: "Registering Taiwan input method..."; \
    Flags: runhidden waituntilterminated
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\CantoIME-MO.dll"""; \
    Check: IsVariant('MO'); StatusMsg: "Registering Macao SAR input method..."; \
    Flags: runhidden waituntilterminated
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\CantoIME-CN.dll"""; \
    Check: IsVariant('CN'); StatusMsg: "Registering China (Simplified) input method..."; \
    Flags: runhidden waituntilterminated
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\CantoIME-SG.dll"""; \
    Check: IsVariant('SG'); StatusMsg: "Registering Singapore (Simplified) input method..."; \
    Flags: runhidden waituntilterminated
; Start the dictionary service now (as the original, non-elevated user, so its
; per-user pipe and keyboard check match the IME). It self-gates: on a fresh
; install where the keyboard is not yet added it just exits; on an update where
; the keyboard was already added it stays and serves. Either way the logon
; autostart brings it back on subsequent sessions.
Filename: "{app}\canto-dictd.exe"; \
    StatusMsg: "Starting dictionary service..."; \
    Flags: nowait runhidden runasoriginaluser

[UninstallRun]
; Stop the resident dictionary service first, so its executable can be removed.
Filename: "{sys}\taskkill.exe"; Parameters: "/f /im canto-dictd.exe"; \
    Flags: runhidden; RunOnceId: "StopDictd"
; Unregister BEFORE files are removed. Only the variant DLL actually present in
; {app} gets unregistered (exactly the one that was installed).
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\CantoIME-HK.dll"""; \
    Check: DllPresent('CantoIME-HK.dll'); Flags: runhidden waituntilterminated; RunOnceId: "UnregIME_HK"
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\CantoIME-TW.dll"""; \
    Check: DllPresent('CantoIME-TW.dll'); Flags: runhidden waituntilterminated; RunOnceId: "UnregIME_TW"
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\CantoIME-MO.dll"""; \
    Check: DllPresent('CantoIME-MO.dll'); Flags: runhidden waituntilterminated; RunOnceId: "UnregIME_MO"
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\CantoIME-CN.dll"""; \
    Check: DllPresent('CantoIME-CN.dll'); Flags: runhidden waituntilterminated; RunOnceId: "UnregIME_CN"
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\CantoIME-SG.dll"""; \
    Check: DllPresent('CantoIME-SG.dll'); Flags: runhidden waituntilterminated; RunOnceId: "UnregIME_SG"

[Code]
{ The variant list is built from the user's installed languages (BCP-47 tags in
  HKCU\...\User Profile\Languages). Only installed Chinese languages are offered;
  if none is installed, a message page replaces the selection and blocks install.
  Caveat: when Setup is elevated with a *different* admin account, HKCU is that
  admin's; in the normal single-admin case it is the installing user's. }

var
  gLangs: String;
  gLangsRead: Boolean;
  VariantPage: TInputOptionWizardPage;
  NoLangPage: TOutputMsgWizardPage;
  AvailTags: array of String;   { parallel to VariantPage radio indices }

function UserLanguages(): String;
var
  S: String;
begin
  if not gLangsRead then
  begin
    if RegQueryMultiStringValue(HKEY_CURRENT_USER,
         'Control Panel\International\User Profile', 'Languages', S) then
      gLangs := Lowercase(S)
    else
      gLangs := '';
    gLangsRead := True;
  end;
  Result := gLangs;
end;

function HasTag(const Tag: String): Boolean;
begin
  Result := Pos(Tag, UserLanguages()) > 0;
end;

function VariantInstalled(const Tag: String): Boolean;
begin
  if Tag = 'HK' then
    Result := HasTag('zh-hant-hk') or HasTag('zh-hk')
  else if Tag = 'TW' then
    Result := HasTag('zh-hant-tw') or HasTag('zh-tw')
  else if Tag = 'MO' then
    Result := HasTag('zh-hant-mo') or HasTag('zh-mo')
  else if Tag = 'CN' then
    Result := HasTag('zh-hans-cn') or HasTag('zh-cn')
  else if Tag = 'SG' then
    Result := HasTag('zh-hans-sg') or HasTag('zh-sg')
  else
    Result := False;
end;

procedure AddVariant(const Tag, Desc: String);
begin
  if VariantInstalled(Tag) then
  begin
    SetArrayLength(AvailTags, GetArrayLength(AvailTags) + 1);
    AvailTags[GetArrayLength(AvailTags) - 1] := Tag;
    VariantPage.Add(Desc);
  end;
end;

procedure InitializeWizard();
var
  anyLang: Boolean;
begin
  anyLang := VariantInstalled('HK') or VariantInstalled('TW') or VariantInstalled('MO')
             or VariantInstalled('CN') or VariantInstalled('SG');

  if anyLang then
  begin
    VariantPage := CreateInputOptionPage(wpWelcome,
      'Select input method',
      'Choose the Chinese variant to install CantoIME for.',
      'Only Chinese languages you have added in Windows are listed. CantoIME will'
        + ' be installed for the selected language.',
      True, False);
    AddVariant('HK', 'Chinese (Traditional, Hong Kong SAR)');
    AddVariant('TW', 'Chinese (Traditional, Taiwan)');
    AddVariant('MO', 'Chinese (Traditional, Macao SAR)');
    AddVariant('CN', 'Chinese (Simplified, China)');
    AddVariant('SG', 'Chinese (Simplified, Singapore)');
    VariantPage.SelectedValueIndex := 0;
  end
  else
  begin
    NoLangPage := CreateOutputMsgPage(wpWelcome,
      'No Chinese language installed',
      'CantoIME needs a Chinese language',
      'No Chinese language is installed on this account.'#13#10#13#10
        + 'Add one first: Settings > Time & language > Language & region > Add a'
        + ' language (for example "Chinese (Traditional, Hong Kong SAR)" or'
        + ' "Chinese (Simplified, China)"). Then run this installer again.');
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  { With no Chinese language installed, block progress past the message page. }
  if (NoLangPage <> nil) and (CurPageID = NoLangPage.ID) then
    WizardForm.NextButton.Enabled := False;
end;

function SelectedVariant(): String;
begin
  if VariantPage <> nil then
    Result := AvailTags[VariantPage.SelectedValueIndex]
  else
    Result := '';
end;

function IsVariant(Tag: String): Boolean;
begin
  Result := SelectedVariant() = Tag;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  { Before files are copied, stop any running (previous) dictionary service so an
    update can replace its executable rather than deferring to a reboot. }
  if CurStep = ssInstall then
    Exec(ExpandConstant('{sys}\taskkill.exe'), '/f /im canto-dictd.exe', '',
         SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

function DllPresent(Name: String): Boolean;
begin
  Result := FileExists(ExpandConstant('{app}\') + Name);
end;
