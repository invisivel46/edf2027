param([Parameter(Mandatory=$true)][int]$GameProcessId,
      [Parameter(Mandatory=$true)][string]$OutputPath,
      [string]$TitleContains='')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class EdfWindowCapture {
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window,out Rect rect);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window,IntPtr dc,uint flags);
  public delegate bool WindowCallback(IntPtr window,IntPtr data);
  [DllImport("user32.dll")] static extern bool EnumWindows(WindowCallback callback,IntPtr data);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint process);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window,System.Text.StringBuilder text,int count);
  public static IntPtr LargestWindow(int process,string titleContains) {
    IntPtr result=IntPtr.Zero; long area=0;
    EnumWindows((window,data)=> {
      uint owner; GetWindowThreadProcessId(window,out owner); Rect rect;
      var title=new System.Text.StringBuilder(512); GetWindowText(window,title,title.Capacity);
      if (!String.IsNullOrEmpty(titleContains) && title.ToString().IndexOf(titleContains,StringComparison.OrdinalIgnoreCase)<0) return true;
      if (owner==process && GetWindowRect(window,out rect)) {
        long candidate=(long)(rect.Right-rect.Left)*(rect.Bottom-rect.Top);
        if (candidate>area) { area=candidate; result=window; }
      }
      return true;
    },IntPtr.Zero);
    return result;
  }
}
'@
$taskGame=Get-Process -Id $GameProcessId
$taskWindow=[EdfWindowCapture]::LargestWindow($taskGame.Id,$TitleContains)
if ($taskWindow -eq [IntPtr]::Zero) { throw 'Game has no main window' }
$taskRect=New-Object EdfWindowCapture+Rect
if (-not [EdfWindowCapture]::GetWindowRect($taskWindow,[ref]$taskRect)) { throw 'Cannot read window bounds' }
$taskBitmap=New-Object System.Drawing.Bitmap ($taskRect.Right-$taskRect.Left),($taskRect.Bottom-$taskRect.Top)
try {
  $taskGraphics=[System.Drawing.Graphics]::FromImage($taskBitmap)
  try {
    $taskDc=$taskGraphics.GetHdc()
    try {
      if (-not [EdfWindowCapture]::PrintWindow($taskWindow,$taskDc,2)) { throw 'Window capture failed' }
    } finally { $taskGraphics.ReleaseHdc($taskDc) }
  } finally { $taskGraphics.Dispose() }
  $taskBitmap.Save([System.IO.Path]::GetFullPath($OutputPath),[System.Drawing.Imaging.ImageFormat]::Png)
} finally { $taskBitmap.Dispose() }
