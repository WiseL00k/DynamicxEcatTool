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
  LaunchWorks: Boolean;
  InstallerExit, RunCalls, Cases: Integer;

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

procedure Scenario(Launch: Boolean; Code: Integer;
  ExpectSuccess, ExpectRestart, ExpectFinalRestart: Boolean);
var
  Error: String;
  Restart: Boolean;
begin
  LaunchWorks := Launch;
  InstallerExit := Code;
  RunCalls := 0;
  RuntimeNeedsRestart := False;
  Restart := False;
  Error := EnsureNpcap(Restart);
  Check((Error = '') = ExpectSuccess, 'Unexpected success/error');
  Check(Restart = ExpectRestart, 'Unexpected blocking restart');
  Check(RuntimeNeedsRestart = ExpectFinalRestart, 'Unexpected final restart');
  Check(RunCalls = 1, 'Embedded installer must be launched on every attempt');
  Cases := Cases + 1;
end;

function InitializeSetup: Boolean;
var
  Report: String;
begin
  Result := False; { Never perform an installation, even when a test fails. }
  Report := ExpandConstant('{param:Report}');
  try
    Scenario(True, 0, True, False, False);
    Scenario(False, 0, False, False, False); { launch failure }
    Scenario(True, 1, False, False, False); { cancelled }
    Scenario(True, 2, False, False, False); { installation failure }
    Scenario(True, 1618, False, False, False); { another installer running }
    Scenario(True, 350, False, True, False);
    Scenario(True, 3010, True, False, True);
    SaveStringToFile(Report, 'PASS ' + IntToStr(Cases), False);
  except
    SaveStringToFile(Report, 'FAIL: ' + GetExceptionMessage, False);
  end;
end;
