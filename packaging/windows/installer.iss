#ifndef AppVersion
  #error Compile with package.ps1 to supply version and deployment paths.
#endif

[Setup]
AppId=Dynamicx EtherCAT Tool
AppName=Dynamicx EtherCAT Tool
AppVersion={#AppVersion}
VersionInfoVersion={#AppVersion}.0
DefaultDirName={autopf}\DynamicxEcatTool
DefaultGroupName=DynamicxEcatTool
ArchitecturesAllowed=x64compatible
PrivilegesRequired=admin
OutputDir={#ReleaseOutput}
OutputBaseFilename=DynamicxEcatTool-v{#AppVersion}-amd64
SetupIconFile={#AssetSource}\dynamicx_logo.ico
WizardSmallImageFile={#AssetSource}\header.bmp
Compression=lzma2
SolidCompression=yes
UninstallDisplayIcon={app}\DynamicxEcatTool.exe

[Files]
Source: "{#AppSource}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#VcRedistPath}"; DestDir: "{tmp}"; DestName: "vc_redist.x64.exe"; Flags: dontcopy
Source: "{#NpcapInstallerPath}"; DestDir: "{tmp}"; DestName: "npcap-{#NpcapVersion}.exe"; Flags: dontcopy

[Icons]
Name: "{group}\DynamicxEcatTool"; Filename: "{app}\DynamicxEcatTool.exe"; WorkingDir: "{app}"
Name: "{commondesktop}\DynamicxEcatTool"; Filename: "{app}\DynamicxEcatTool.exe"; WorkingDir: "{app}"

[Code]
var
  RuntimeNeedsRestart: Boolean;
  RuntimeInstalled: Boolean;

function RunNpcap(var ExitCode: Integer): Boolean;
begin
  ExtractTemporaryFile('npcap-{#NpcapVersion}.exe');
  Result := Exec(ExpandConstant('{tmp}\npcap-{#NpcapVersion}.exe'),
    '/winpcap_mode=enforced', '', SW_SHOW, ewWaitUntilTerminated, ExitCode);
end;

#include "npcap-state.iss"

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ExitCode: Integer;
begin
  Result := '';
  { Install the runtime first: Npcap and the application may need it.
    App files are not installed until both prerequisites succeed. }
  if not RuntimeInstalled then
  begin
    ExtractTemporaryFile('vc_redist.x64.exe');
    if not Exec(ExpandConstant('{tmp}\vc_redist.x64.exe'),
        '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, ExitCode) then
    begin
      Result := '无法启动 Microsoft Visual C++ x64 运行库安装程序。';
      Exit;
    end;
    if (ExitCode <> 0) and (ExitCode <> 1638) and (ExitCode <> 3010) then
    begin
      Result := Format('Microsoft Visual C++ x64 运行库安装失败（退出码 %d）。', [ExitCode]);
      Exit;
    end;
    if ExitCode = 3010 then RuntimeNeedsRestart := True;
    RuntimeInstalled := True;
  end;
  Result := EnsureNpcap(NeedsRestart);
end;

function NeedRestart: Boolean;
begin
  Result := RuntimeNeedsRestart;
end;
