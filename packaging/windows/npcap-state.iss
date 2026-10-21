// Shared embedded-installer exit handling; IO is supplied by the installer or tests.
function EnsureNpcap(var NeedsRestart: Boolean): String;
var
  ExitCode: Integer;
begin
  Result := '';
  if not RunNpcap(ExitCode) then
  begin
    Result := '无法启动内嵌的 Npcap 安装程序。请重试或取消安装。';
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
end;
