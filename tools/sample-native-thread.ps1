param([Parameter(Mandatory=$true)][int]$GameProcessId,[int]$GameThreadId=0)
$ErrorActionPreference='Stop'
$taskGame=Get-Process -Id $GameProcessId
$taskExpected=[System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../out/build/win-amd64-release/edf2027.exe'))
if ($taskGame.Path -ne $taskExpected) { throw 'Refusing to sample an unexpected executable' }
if (-not $GameThreadId) {
  $GameThreadId=($taskGame.Threads | Sort-Object TotalProcessorTime -Descending | Select-Object -First 1).Id
}
if ($GameThreadId -notin $taskGame.Threads.Id) { throw 'Thread is not owned by this game process' }
if (-not ('EdfNativeThreadSampler' -as [type])) {
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class EdfNativeThreadSampler {
  public sealed class Sample {
    public long Rip,Rsp,Rbp,Rbx,Rdi,Rsi,R12,R13,R14,R15;
  }
  [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenThread(uint access,bool inherit,uint id);
  [DllImport("kernel32.dll")] static extern uint GetProcessIdOfThread(IntPtr thread);
  [DllImport("kernel32.dll")] static extern uint SuspendThread(IntPtr thread);
  [DllImport("kernel32.dll")] static extern uint ResumeThread(IntPtr thread);
  [DllImport("kernel32.dll")] static extern bool GetThreadContext(IntPtr thread,IntPtr context);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
  public static Sample Read(uint process,uint thread) {
    if(IntPtr.Size!=8) throw new Exception("Sampler requires x64 PowerShell");
    var handle=OpenThread(0x080a,false,thread);
    if(handle==IntPtr.Zero) throw new Exception("OpenThread failed: "+Marshal.GetLastWin32Error());
    IntPtr storage=IntPtr.Zero; bool suspended=false;
    try {
      if(GetProcessIdOfThread(handle)!=process) throw new Exception("Thread owner changed");
      storage=Marshal.AllocHGlobal(1248);
      var context=new IntPtr((storage.ToInt64()+15)&~15L);
      for(int i=0;i<1232;++i) Marshal.WriteByte(context,i,0);
      Marshal.WriteInt32(context,48,0x100003); // AMD64 CONTROL | INTEGER.
      if(SuspendThread(handle)==uint.MaxValue) throw new Exception("SuspendThread failed");
      suspended=true;
      if(!GetThreadContext(handle,context)) throw new Exception("GetThreadContext failed");
      return new Sample {Rip=Marshal.ReadInt64(context,248),Rsp=Marshal.ReadInt64(context,152),
        Rbp=Marshal.ReadInt64(context,160),Rbx=Marshal.ReadInt64(context,144),
        Rdi=Marshal.ReadInt64(context,176),Rsi=Marshal.ReadInt64(context,168),
        R12=Marshal.ReadInt64(context,216),R13=Marshal.ReadInt64(context,224),
        R14=Marshal.ReadInt64(context,232),R15=Marshal.ReadInt64(context,240)};
    } finally {
      if(suspended) ResumeThread(handle);
      if(storage!=IntPtr.Zero) Marshal.FreeHGlobal(storage);
      CloseHandle(handle);
    }
  }
}
'@
}
$taskSample=[EdfNativeThreadSampler]::Read($GameProcessId,$GameThreadId)
$taskModule=$taskGame.Modules | Where-Object {
  $taskSample.Rip -ge $_.BaseAddress.ToInt64() -and
  $taskSample.Rip -lt ($_.BaseAddress.ToInt64()+$_.ModuleMemorySize)
} | Select-Object -First 1
[pscustomobject]@{
  Process=$GameProcessId; Thread=$GameThreadId; Module=$taskModule.ModuleName
  Rip=('0x{0:X}' -f $taskSample.Rip)
  ModuleOffset=if($taskModule){'0x{0:X}' -f ($taskSample.Rip-$taskModule.BaseAddress.ToInt64())}else{'unknown'}
  Rsp=('0x{0:X}' -f $taskSample.Rsp); Rbp=('0x{0:X}' -f $taskSample.Rbp)
  Rbx=('0x{0:X}' -f $taskSample.Rbx); Rdi=('0x{0:X}' -f $taskSample.Rdi)
  Rsi=('0x{0:X}' -f $taskSample.Rsi); R12=('0x{0:X}' -f $taskSample.R12)
  R13=('0x{0:X}' -f $taskSample.R13); R14=('0x{0:X}' -f $taskSample.R14)
  R15=('0x{0:X}' -f $taskSample.R15)
} | Format-List
