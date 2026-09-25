param([int]$ProcessId, [string]$Out, [double]$Interval = 1.0, [int]$Seconds = 900)
# Per-thread CPU time sampler: every $Interval s, writes one CSV row per thread with CPU ms used in that interval and its description.
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class TD {
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenThread(uint access, bool inherit, uint id);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32.dll")] public static extern int GetThreadDescription(IntPtr h, out IntPtr desc);
  [DllImport("kernel32.dll")] public static extern IntPtr LocalFree(IntPtr p);
  [DllImport("kernel32.dll")] public static extern bool QueryThreadCycleTime(IntPtr h, out ulong cycles);
  public static string Desc(uint id) {
    IntPtr h = OpenThread(0x1000, false, id); if (h == IntPtr.Zero) return "";
    IntPtr p; string s = "";
    if (GetThreadDescription(h, out p) >= 0 && p != IntPtr.Zero) { s = Marshal.PtrToStringUni(p); LocalFree(p); }
    CloseHandle(h); return s;
  }
}
"@
$names = @{}
$prev = @{}
$w = [System.IO.StreamWriter]::new($Out)
$w.WriteLine('t_s,tid,name,cpu_ms')
$start = Get-Date
$p = Get-Process -Id $ProcessId
while (-not $p.HasExited -and ((Get-Date) - $start).TotalSeconds -lt $Seconds) {
  $p.Refresh()
  $t = ((Get-Date) - $start).TotalSeconds
  foreach ($th in $p.Threads) {
    try { $cpu = $th.TotalProcessorTime.TotalMilliseconds } catch { continue }
    $id = [uint32]$th.Id
    if (-not $names.ContainsKey($id)) { $names[$id] = ([TD]::Desc($id) -replace ',', ';') }
    if ($prev.ContainsKey($id)) {
      $d = $cpu - $prev[$id]
      if ($d -gt 0.5) { $w.WriteLine(('{0:F1},{1},{2},{3:F1}' -f $t, $id, $names[$id], $d)) }
    }
    $prev[$id] = $cpu
  }
  $others = @(Get-Process edf2027* -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $ProcessId }).Count
  $builds = @(Get-Process clang*,ninja*,lld* -ErrorAction SilentlyContinue).Count
  if ($others -gt 0 -or $builds -gt 0) { $w.WriteLine(('{0:F1},0,OTHER_GAMES={1};BUILDS={2},0' -f $t, $others, $builds)) }
  $w.Flush()
  Start-Sleep -Milliseconds ([int]($Interval * 1000))
}
$w.Close()
