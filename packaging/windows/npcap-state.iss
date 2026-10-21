// Shared production state machine; IO is supplied by installer.iss or the test harness.
function EnsureNpcap(var NeedsRestart: Boolean): String;
var
  ExitCode: Integer;
begin
  Result := '';
  if HaveCompatibleNpcap then Exit;

  Result := FetchNpcap;
  if Result <> '' then Exit;
  if not RunNpcap(ExitCode) then
  begin
    Result := '无法启动 Npcap 安装程序。请重试或取消安装。';
    Exit;
  end;
  if ExitCode = 350 then
  begin
    NeedsRestart := True;
    Result := 'Npcap 安装失败，需要重启电脑后重新运行安装程序。';
    Exit;
  end;
  if (ExitCode <> 0) and (ExitCode <> 3010) then
  begin
    Result := Format('Npcap 安装已取消或失败（退出码 %d）。请重试并完成安装，或取消本次安装。', [ExitCode]);
    Exit;
  end;
  if ExitCode = 3010 then RuntimeNeedsRestart := True;
  if not HaveCompatibleNpcap then
  begin
    if ExitCode = 3010 then
    begin
      NeedsRestart := True;
      Result := 'Npcap 需要重启电脑。请重启后重新运行本软件安装程序。';
    end else
      Result := '未检测到兼容的 Npcap。请启用 WinPcap API-compatible Mode 并完成安装，然后重试。';
  end;
end;
