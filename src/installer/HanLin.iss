#define MyAppName "寒淋输入法"
#define MyAppVersion "1.3.0"
#define MyAppVersionQuad MyAppVersion + ".0"
#define MyAppPublisher "TangSong777"
#define MyAppURL "https://github.com/TangSong777/HanLin-IME"
#define SetupIntroZh "寒淋是 Windows 本地拼音输入法。安装程序需要管理员权限来注册输入服务。"
#define SetupIntroEn "HanLin is a local Pinyin input method. Windows requires administrator permission to register the input service."

#ifndef PayloadDir
  #error Use build-installer.ps1 to prepare PayloadDir.
#endif

#include "Brand\HanLinWizard.iss"

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
LicenseFile=payload\LICENSE.txt
OutputDir=output
OutputBaseFilename=HanLin-Setup-{#MyAppVersion}
SetupIconFile=..\HanLin\icons\hanlin.ico
PrivilegesRequired=admin
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\HanLinHost.exe

[Files]
Source: "{#PayloadDir}\HanLinHost.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\HanLinTools.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\HanLinTools.exe.config"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\Sync-HanLin.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\rime.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\Migrate-HanLinUserData.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\x64\HanLinTip.dll"; DestDir: "{app}\x64"; Flags: ignoreversion regserver 64bit
Source: "{#PayloadDir}\x86\HanLinTip.dll"; DestDir: "{app}\x86"; Flags: ignoreversion regserver 32bit
Source: "{#PayloadDir}\data\*"; DestDir: "{app}\RuntimeData"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PayloadDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs

[InstallDelete]
Type: files; Name: "{app}\tools\WordManager.vbs"
Type: files; Name: "{app}\tools\WordManager.ps1"
Type: files; Name: "{app}\tools\WordManagerData.ps1"
Type: files; Name: "{app}\tools\WordManagerDeployment.ps1"
Type: files; Name: "{app}\tools\gen-mixed-tokens.ps1"

[Registry]
Root: HKLM64; Subkey: "SOFTWARE\HanLin"; ValueType: string; ValueName: "InstallRoot"; ValueData: "{app}"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM32; Subkey: "SOFTWARE\HanLin"; ValueType: string; ValueName: "InstallRoot"; ValueData: "{app}"; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM64; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "HanLin"; ValueData: """{app}\HanLinHost.exe"""; Flags: uninsdeletevalue

[Icons]
Name: "{group}\启动寒淋输入法"; Filename: "{app}\HanLinHost.exe"
Name: "{group}\设置"; Filename: "{app}\HanLinTools.exe"; Parameters: "--settings"
Name: "{group}\自定义词条"; Filename: "{app}\HanLinTools.exe"; Parameters: "--words"
Name: "{group}\打字统计"; Filename: "{app}\HanLinTools.exe"; Parameters: "--stats"
Name: "{group}\卸载寒淋输入法"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\HanLinHost.exe"; Parameters: "--enable-profile"; Flags: runasoriginaluser runhidden waituntilterminated; Check: StartAfterSetup
Filename: "{app}\HanLinHost.exe"; Description: "{cm:HanLinLaunch,{#MyAppName}}"; Flags: postinstall runasoriginaluser nowait skipifsilent; Check: StartAfterSetup
Filename: "{app}\HanLinHost.exe"; Flags: runasoriginaluser nowait skipifnotsilent; Check: StartAfterSetup

[UninstallRun]
Filename: "{app}\HanLinHost.exe"; Parameters: "--stop"; Flags: runhidden waituntilterminated; RunOnceId: "StopHanLinHost"

[Code]
function StartAfterSetup: Boolean;
begin
  Result := ExpandConstant('{param:HANLINDEFERSTART|0}') <> '1';
end;

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
    Result := '所选目录中已有其他输入法。请选择独立的寒淋安装目录。';
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
