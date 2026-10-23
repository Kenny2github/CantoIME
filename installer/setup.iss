; setup.iss  -  Inno Setup script for CantoIME
;
; Builds CantoIME-Setup.exe. It offers ONE Chinese variant to install for (only
; the languages the user has already added in Windows are listed), and for the
; chosen variant:
;   * installs its DLL to {autopf}\CantoIME (64-bit),
;   * installs the stock dictionaries (traditional.txt, simplified.txt) alongside,
;   * registers the TSF text service via 64-bit regsvr32 on install,
;   * unregisters it via regsvr32 /u on uninstall (before files are removed),
;   * on the Finished page, optionally (checkbox, on by default) adds the keyboard
;     to the installing user's keyboards for the chosen language.
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
; Cache builder. Run post-install to prebuild the stock dictionaries' mmap
; caches, and by the options dialog when a custom dictionary is locked in.
Source: "..\build\Release\canto-mkcache.exe"; DestDir: "{app}"; Flags: ignoreversion 64bit

[Registry]
; Clean up the per-user options key on uninstall (not created at install time;
; the IME writes it when the user changes options).
Root: HKCU; Subkey: "Software\CantoIME"; Flags: uninsdeletekey dontcreatekey

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
; Prebuild the mmap cache for BOTH stock dictionaries. They are machine-wide,
; so the caches live next to them and serve every user; the IME maps them
; instead of parsing 10+ MB of text. Custom dictionaries are per-user and are
; cached under %LOCALAPPDATA% (by the options dialog, or on first load).
Filename: "{app}\canto-mkcache.exe"; \
    Parameters: """{app}\traditional.txt"" ""{app}\simplified.txt"""; \
    StatusMsg: "Preparing dictionaries..."; Flags: runhidden waituntilterminated

[UninstallDelete]
; Caches are generated after install (not tracked as installed files), so remove
; them explicitly, including any ".old" left from replacing a mapped cache, plus
; the per-user cache folder.
Type: files; Name: "{app}\*.cache"
Type: files; Name: "{app}\*.cache.old"
Type: filesandordirs; Name: "{localappdata}\CantoIME"

[UninstallRun]
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
  AddKbdText: TNewStaticText;   { on the Finished page }
  AddKbdCheck: TNewCheckBox;

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

procedure UpdateAddKbdCaption();
begin
  if WizardForm.YesRadio.Visible and WizardForm.YesRadio.Checked then
    AddKbdCheck.Caption := 'Add the CantoIME keyboard before restarting'
  else
    AddKbdCheck.Caption := 'Add the CantoIME keyboard now';
end;

procedure RestartRadioClick(Sender: TObject);
begin
  UpdateAddKbdCaption();
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

    { Offer to add the keyboard on the Finished page (laid out in CurPageChanged,
      once Setup knows whether it will ask to restart). }
    AddKbdText := TNewStaticText.Create(WizardForm);
    AddKbdText.Parent := WizardForm.FinishedPage;
    AddKbdText.WordWrap := True;
    AddKbdCheck := TNewCheckBox.Create(WizardForm);
    AddKbdCheck.Parent := WizardForm.FinishedPage;
    AddKbdCheck.Checked := True;
    WizardForm.YesRadio.OnClick := @RestartRadioClick;
    WizardForm.NoRadio.OnClick := @RestartRadioClick;
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
var
  Y: Integer;
begin
  { With no Chinese language installed, block progress past the message page. }
  if (NoLangPage <> nil) and (CurPageID = NoLangPage.ID) then
    WizardForm.NextButton.Enabled := False;

  { Finished page: put the add-keyboard text + checkbox below the restart
    choice if Setup asks to restart, else below the finished text. }
  if (CurPageID = wpFinished) and (AddKbdCheck <> nil) then
  begin
    if WizardForm.NoRadio.Visible then
      Y := WizardForm.NoRadio.Top + WizardForm.NoRadio.Height + ScaleY(16)
    else
      Y := WizardForm.FinishedLabel.Top + WizardForm.FinishedLabel.Height;
    AddKbdText.SetBounds(WizardForm.FinishedLabel.Left, Y,
      WizardForm.FinishedLabel.Width, ScaleY(40));
    AddKbdText.Caption := 'The "CantoIME" keyboard has now been registered under the "'
      + VariantPage.CheckListBox.ItemCaption[VariantPage.SelectedValueIndex]
      + '" language. You can now add the keyboard to your available keyboards in'
      + ' Settings > Time & language > Language & region, or by leaving the box'
      + ' below checked.';
    AddKbdText.AdjustHeight();
    AddKbdCheck.SetBounds(WizardForm.FinishedLabel.Left,
      AddKbdText.Top + AddKbdText.Height + ScaleY(12),
      WizardForm.FinishedLabel.Width, ScaleY(17));
    UpdateAddKbdCaption();
  end;
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

function DllPresent(Name: String): Boolean;
begin
  Result := FileExists(ExpandConstant('{app}\') + Name);
end;

{ Input-method tip of each variant, as "LANGID:" + CLSID + profile GUID. Must
  match TEXTSERVICE_LANGID (Globals.h) and CLSID_CantoIME / GUID_Profile
  (Globals.cpp). Tags are the forms VariantInstalled accepts. }
procedure VariantTip(const Tag: String; var Tip, Tags: String);
begin
  if Tag = 'HK' then
  begin
    Tip := '0C04:{BC8FB43B-879A-4B9C-8723-3FE7556D57E2}{38F83831-D93F-473A-97B7-8E590D41A72D}';
    Tags := '''zh-Hant-HK'',''zh-HK''';
  end
  else if Tag = 'TW' then
  begin
    Tip := '0404:{C732EC0C-99F6-43EC-83C5-42EC1C944325}{973BC8C9-A5A2-4BC6-90AB-8CF4EA25FFBF}';
    Tags := '''zh-Hant-TW'',''zh-TW''';
  end
  else if Tag = 'MO' then
  begin
    Tip := '1404:{7E96AB83-F0BF-4F24-B41F-2E32BD562DA6}{2CB45F66-00A5-418C-8354-87AC7B7C8C85}';
    Tags := '''zh-Hant-MO'',''zh-MO''';
  end
  else if Tag = 'CN' then
  begin
    Tip := '0804:{7C5C8388-430C-4C15-8A87-78A0BB9A482B}{1307330C-CDBA-484F-891F-FEB4595D437D}';
    Tags := '''zh-Hans-CN'',''zh-CN''';
  end
  else
  begin
    Tip := '1004:{B551180B-E874-4B04-8B8F-3F16344AD683}{43CCFF03-2E82-4938-88CB-15D4CBE069A7}';
    Tags := '''zh-Hans-SG'',''zh-SG''';
  end;
end;

{ Adds the keyboard to the user's language list. Run as the original
  (non-elevated) user so it lands in their list, not an elevating admin's. }
procedure AddKeyboard();
var
  Tip, Tags: String;
  ResultCode: Integer;
begin
  VariantTip(SelectedVariant(), Tip, Tags);
  if not ExecAsOriginalUser(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
       '-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "'
         + '$ErrorActionPreference = ''Stop''; $tip = ''' + Tip + ''';'
         + ' $list = Get-WinUserLanguageList;'
         + ' $lang = $list | Where-Object { $_.LanguageTag -in ' + Tags + ' } | Select-Object -First 1;'
         + ' if (-not $lang) { exit 1 };'
         + ' if ($lang.InputMethodTips -notcontains $tip) {'
         + ' $lang.InputMethodTips.Add($tip); Set-WinUserLanguageList $list -Force }"',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
    SuppressibleMsgBox('The CantoIME keyboard could not be added automatically. Add it in'
      + ' Settings > Time & language > Language & region instead.',
      mbError, MB_OK, IDOK);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  { Finish clicked: add the keyboard if asked. This runs before Setup exits,
    so before any restart. Silent installs never add it. }
  if (CurPageID = wpFinished) and (AddKbdCheck <> nil) and AddKbdCheck.Checked
     and not WizardSilent() then
    AddKeyboard();
  Result := True;
end;
