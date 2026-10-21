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

[Icons]
Name: "{group}\DynamicxEcatTool"; Filename: "{app}\DynamicxEcatTool.exe"; WorkingDir: "{app}"
Name: "{commondesktop}\DynamicxEcatTool"; Filename: "{app}\DynamicxEcatTool.exe"; WorkingDir: "{app}"

[Code]
var
  DownloadPage: TDownloadWizardPage;
  NetworkInfoPage: TOutputMsgWizardPage;
  RuntimeNeedsRestart: Boolean;
  RuntimeInstalled: Boolean;

function IsNpcapDll(const Path: String): Boolean;
var
  MS, LS: Cardinal;
begin
  Result := GetVersionNumbers(Path, MS, LS) and ((MS shr 16) = 5);
end;

function HaveCompatibleNpcap: Boolean;
var
  Compatible: Cardinal;
begin
  Result := RegKeyExists(HKLM64, 'SYSTEM\CurrentControlSet\Services\npcap') and
    RegQueryDWordValue(HKLM64, 'SYSTEM\CurrentControlSet\Services\npcap\Parameters',
      'WinPcapCompatible', Compatible) and (Compatible = 1) and
    IsNpcapDll(ExpandConstant('{sysnative}\wpcap.dll')) and
    IsNpcapDll(ExpandConstant('{sysnative}\Packet.dll'));
end;

procedure InitializeWizard;
begin
  NetworkInfoPage := CreateOutputMsgPage(wpWelcome, 'Npcap 网络依赖', '首次安装提示',
    '此电脑首次安装本软件且尚未安装兼容的 Npcap 时，需要联网下载并安装 Npcap。' + #13#10#13#10 +
    '下载完成后将自动打开 Npcap 安装程序，请按提示完成安装。' + #13#10#13#10 +
    '请启用 WinPcap API-compatible Mode。若已安装兼容的 Npcap，可离线安装本软件。');
  DownloadPage := CreateDownloadPage('下载 Npcap', '请保持网络连接，下载完成后将自动打开安装程序。', nil);
  DownloadPage.ShowBaseNameInsteadOfUrl := True;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = NetworkInfoPage.ID) and HaveCompatibleNpcap;
end;

function FetchNpcap: String;
var
  DownloadPath: String;
begin
  Result := '';
  DownloadPath := ExpandConstant('{tmp}\npcap-{#NpcapVersion}.exe');
  DownloadPage.Clear;
  { Hash separately with Inno's native SHA256 API to distinguish integrity errors
    from network errors without depending on translated exception strings. }
  DownloadPage.Add('{#NpcapUrl}', 'npcap-{#NpcapVersion}.exe', '');
  DownloadPage.Show;
  try
    try
      DownloadPage.Download;
      if CompareText(GetSHA256OfFile(DownloadPath), '{#NpcapSHA256}') <> 0 then
      begin
        DeleteFile(DownloadPath);
        Result := 'Npcap 文件校验失败，已禁止运行。请重试或从官方网站安装 Npcap。';
      end;
    except
      Log(GetExceptionMessage);
      if DownloadPage.AbortedByUser then
        Result := 'Npcap 下载已取消。请重试或取消本次安装。'
      else
        Result := '无法下载 Npcap。此电脑首次安装本软件且缺少 Npcap 时，需要联网安装。请检查网络连接后重试，或取消安装。';
    end;
  finally
    DownloadPage.Hide;
  end;
end;

function RunNpcap(var ExitCode: Integer): Boolean;
begin
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
