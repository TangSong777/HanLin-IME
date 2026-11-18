#define MyAppName "寒淋输入法"
#define MyAppVersion "0.2.0"
#define MyAppVersionQuad "0.2.0.0"
#define MyAppPublisher "TangSong777"
#define MyAppURL "https://github.com/TangSong777/HanLin-IME"

#ifndef PayloadDir
  #error Use build-installer.ps1 to prepare PayloadDir.
#endif

[Setup]
AppId={{8F3A7C21-4D6B-4E59-9A2F-7C1B5E8D3A04}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
VersionInfoVersion={#MyAppVersionQuad}
VersionInfoProductName={#MyAppName}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={code:DefaultInstallDir}
DefaultGroupName={#MyAppName}
UsePreviousAppDir=no
DisableProgramGroupPage=yes
LicenseFile=payload\LICENSE.txt
OutputDir=output
OutputBaseFilename=HanLin-Setup-{#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
PrivilegesRequired=admin
WizardStyle=modern
SetupLogging=yes
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\HanLinHost.exe
MinVersion=10.0

[Languages]
Name: "chinese"; MessagesFile: "Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "skin"; Description: "安装现有 Color-P 候选皮肤"; GroupDescription: "外观"; Flags: checkedonce

[Files]
Source: "{#PayloadDir}\HanLinHost.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\rime.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\x64\HanLinTip.dll"; DestDir: "{app}\x64"; Flags: ignoreversion regserver 64bit
Source: "{#PayloadDir}\x86\HanLinTip.dll"; DestDir: "{app}\x86"; Flags: ignoreversion regserver 32bit
Source: "{#PayloadDir}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PayloadDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs
Source: "{#PayloadDir}\skin\*"; DestDir: "{commonappdata}\HanLin\Skin"; Flags: ignoreversion recursesubdirs; Tasks: skin

[Registry]
Root: HKLM64; Subkey: "SOFTWARE\HanLin"; ValueType: string; ValueName: "InstallRoot"; ValueData: "{app}"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM32; Subkey: "SOFTWARE\HanLin"; ValueType: string; ValueName: "InstallRoot"; ValueData: "{app}"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM64; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "HanLin"; ValueData: """{app}\HanLinHost.exe"""; Flags: uninsdeletevalue

[Icons]
Name: "{group}\启动寒淋输入法"; Filename: "{app}\HanLinHost.exe"
Name: "{group}\卸载寒淋输入法"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\HanLinHost.exe"; Parameters: "--enable-profile"; Flags: runasoriginaluser runhidden waituntilterminated
Filename: "{app}\HanLinHost.exe"; Flags: runasoriginaluser runhidden nowait

[UninstallRun]
Filename: "{app}\HanLinHost.exe"; Parameters: "--stop"; Flags: runhidden waituntilterminated; RunOnceId: "StopHanLinHost"

[Code]
function DefaultInstallDir(Param: string): string;
begin
  if not RegQueryStringValue(HKLM64, 'SOFTWARE\HanLin', 'InstallRoot', Result) or (Result = '') then
    Result := ExpandConstant('{autopf}\HanLin');
end;

function PrepareToInstall(var NeedsRestart: Boolean): string;
var
  AppDir, ExistingHost: string;
  ResultCode: Integer;
begin
  Result := '';
  AppDir := WizardDirValue;
  if FileExists(AddBackslash(AppDir) + 'WeaselServer.exe') or
     FileExists(AddBackslash(AppDir) + 'WeaselSetup.exe') then
  begin
    Result := '请选择独立的寒淋安装目录。所选目录中已有小狼毫。';
    Exit;
  end;
  ExistingHost := AddBackslash(AppDir) + 'HanLinHost.exe';
  if FileExists(ExistingHost) then
  begin
    ExtractTemporaryFile('HanLinHost.exe');
    if not Exec(ExpandConstant('{tmp}\HanLinHost.exe'), '--stop', ExpandConstant('{tmp}'), SW_HIDE,
                ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
      Result := '寒淋后端尚未退出。请关闭后重试。';
  end;
end;
