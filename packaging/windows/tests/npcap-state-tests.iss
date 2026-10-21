[Setup]
AppName=Dynamicx Npcap State Tests
AppVersion=1.0
DefaultDirName={tmp}\DynamicxNpcapStateTests
PrivilegesRequired=lowest
Uninstallable=no
CreateAppDir=no
OutputDir={#TestOutput}
OutputBaseFilename=npcap-state-tests

[Code]
var
  RuntimeNeedsRestart: Boolean;
  ReadyBefore, ReadyAfter, LaunchWorks: Boolean;
  DownloadError: String;
  InstallerExit, FetchCalls, RunCalls, Cases: Integer;

function HaveCompatibleNpcap: Boolean;
begin
  if RunCalls = 0 then Result := ReadyBefore else Result := ReadyAfter;
end;

function FetchNpcap: String;
begin
  FetchCalls := FetchCalls + 1;
  Result := DownloadError;
end;

function RunNpcap(var ExitCode: Integer): Boolean;
begin
  RunCalls := RunCalls + 1;
  ExitCode := InstallerExit;
  Result := LaunchWorks;
end;

#include "..\npcap-state.iss"

procedure Check(Condition: Boolean; const Message: String);
begin
  if not Condition then RaiseException(Message);
end;

procedure Scenario(BeforeReady, AfterReady, Launch: Boolean; const DownloadFailure: String;
  Code: Integer; ExpectSuccess, ExpectRestart, ExpectFinalRestart: Boolean;
  ExpectedFetch, ExpectedRun: Integer);
var
  Error: String;
  Restart: Boolean;
begin
  ReadyBefore := BeforeReady;
  ReadyAfter := AfterReady;
  LaunchWorks := Launch;
  DownloadError := DownloadFailure;
  InstallerExit := Code;
  FetchCalls := 0;
  RunCalls := 0;
  RuntimeNeedsRestart := False;
  Restart := False;
  Error := EnsureNpcap(Restart);
  Check((Error = '') = ExpectSuccess, 'Unexpected success/error');
  Check(Restart = ExpectRestart, 'Unexpected blocking restart');
  Check(RuntimeNeedsRestart = ExpectFinalRestart, 'Unexpected final restart');
  Check(FetchCalls = ExpectedFetch, 'Unexpected download count');
  Check(RunCalls = ExpectedRun, 'Unexpected installer launch count');
  if DownloadFailure <> '' then Check(Error = DownloadFailure, 'Download error was lost');
  Cases := Cases + 1;
end;

function InitializeSetup: Boolean;
var
  Report: String;
begin
  Result := False; { Never perform an installation, even when a test fails. }
  Report := ExpandConstant('{param:Report}');
  try
    Scenario(True, True, True, '', 0, True, False, False, 0, 0); { installed + offline }
    Scenario(False, True, True, '', 0, True, False, False, 1, 1);
    Scenario(False, False, True, 'network unavailable', 0, False, False, False, 1, 0);
    Scenario(False, False, True, 'SHA256 mismatch', 0, False, False, False, 1, 0);
    Scenario(False, False, True, 'download cancelled', 0, False, False, False, 1, 0);
    Scenario(False, False, False, '', 0, False, False, False, 1, 1);
    Scenario(False, False, True, '', 1, False, False, False, 1, 1); { cancelled }
    Scenario(False, True, True, '', 2, False, False, False, 1, 1); { no false success }
    Scenario(False, False, True, '', 0, False, False, False, 1, 1); { bad config }
    Scenario(False, False, True, '', 350, False, True, False, 1, 1);
    Scenario(False, True, True, '', 3010, True, False, True, 1, 1);
    Scenario(False, False, True, '', 3010, False, True, True, 1, 1);
    SaveStringToFile(Report, 'PASS ' + IntToStr(Cases), False);
  except
    SaveStringToFile(Report, 'FAIL: ' + GetExceptionMessage, False);
  end;
end;
