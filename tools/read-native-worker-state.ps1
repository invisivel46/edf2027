param([Parameter(Mandatory=$true)][int]$ProcessId,
      [string]$ExecutableName='edf2027.exe',
      [switch]$InspectEnvironment)
$ErrorActionPreference='Stop'
$process=Get-Process -Id $ProcessId
if($ExecutableName -notmatch '^edf2027(?:-[a-zA-Z0-9-]+)?\.exe$') {
  throw 'Expected an EDF executable basename, not a path'
}
$expected=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../out/build/win-native-clean/$ExecutableName"))
if($process.Path -ne $expected){throw "Unexpected process path: $($process.Path)"}
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NativeWorkerReadOnly {
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access,bool inherit,int pid);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr process,IntPtr address,[Out] byte[] data,UIntPtr size,out UIntPtr read);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
}
'@
$handle=[NativeWorkerReadOnly]::OpenProcess(0x10,$false,$ProcessId)
if($handle -eq [IntPtr]::Zero){throw 'Cannot open process for read-only inspection'}
function Read-GuestWord([uint32]$Address) {
  $data=New-Object byte[] 4
  $read=[UIntPtr]::Zero
  $hostAddress=[IntPtr]([long]0x100000000+[long]$Address)
  # Match this build's Windows REX_PHYS_HOST_OFFSET, including the top alias.
  if($Address -ge 0xe0000000L){$hostAddress=[IntPtr]($hostAddress.ToInt64()+0x1000)}
  if(-not [NativeWorkerReadOnly]::ReadProcessMemory($handle,$hostAddress,$data,[UIntPtr]::new([uint32]4),[ref]$read) -or $read.ToUInt64() -ne 4){
    throw ('Read failed at guest 0x{0:x8}' -f $Address)
  }
  [Array]::Reverse($data)
  [BitConverter]::ToUInt32($data,0)
}
try {
  if($InspectEnvironment) {
    # VM command 101 calls 820B5718 with the manager at 82580000-31112.
    # Uploader selects a 172-byte record via map+28 relative to map base.
    $environmentManager=Read-GuestWord (0x82580000L-31112)
    if(-not $environmentManager){throw 'Environment manager is not initialized'}
    $environmentMap=Read-GuestWord ($environmentManager+124)
    if(-not $environmentMap){throw 'Environment map is not initialized'}
    $environmentCount=Read-GuestWord ($environmentMap+24)
    if($environmentCount -gt 64){throw 'Unexpected environment preset count'}
    $environmentTable=[long]$environmentMap+(Read-GuestWord ($environmentMap+28))
    'environment_manager=0x{0:x8} map=0x{1:x8} presets={2} (non-atomic snapshot)' -f $environmentManager,$environmentMap,$environmentCount
    for($preset=0;$preset -lt $environmentCount;$preset++) {
      $record=$environmentTable+$preset*172
      foreach($field in @(@('MiddleGray',108),@('LuminanceWhite',112),@('ToneMap',116))) {
        $bits=Read-GuestWord ($record+$field[1])
        $number=[BitConverter]::ToSingle([BitConverter]::GetBytes([uint32]$bits),0)
        'environment[{0}].{1}={2} bits=0x{3:x8}' -f $preset,$field[0],$number,$bits
      }
    }
  }
  # 821FA0D8 forwards GPU statistic floats through this optional callback.
  $statisticsGlobal=Read-GuestWord 0x82000800L
  $statisticsObject=if($statisticsGlobal){Read-GuestWord $statisticsGlobal}else{0}
  $statisticsCallback=if($statisticsObject){Read-GuestWord ($statisticsObject+24)}else{0}
  'statistics_global=0x{0:x8} object=0x{1:x8} callback=0x{2:x8} (non-atomic snapshot)' -f $statisticsGlobal,$statisticsObject,$statisticsCallback
  $global=Read-GuestWord 0x82000720L
  'device_global=0x{0:x8}' -f $global
  $device=Read-GuestWord $global
  # CF60 conditionally waits for issued-2 only when this SDK global is set.
  'recording_sync_mode={0} (CF60 optional fence wait)' -f (Read-GuestWord (0x82580000L-29432))
  'PID={0} device=0x{1:x8} time={2:o}' -f $ProcessId,$device,(Get-Date)
  # 82147028 reads byte (0x82580000-29416+260) and device byte20400.
  # Both are word-aligned; the first byte of a BE word is the high byte.
  $presentGlobalByte=(Read-GuestWord 0x82578e1cL) -shr 24
  $presentDeviceByte=(Read-GuestWord ($device+20400)) -shr 24
  $presentBranch=if($presentGlobalByte) {
    if($presentDeviceByte -band 8){'821465C8'}else{'82146CC0'}
  } else {
    if($presentDeviceByte -band 8){'821462D0'}else{'return'}
  }
  'presentation_dispatch_global=0x{0:x2} device_flags=0x{1:x2} selected={2} (non-atomic snapshot)' -f $presentGlobalByte,$presentDeviceByte,$presentBranch
  foreach($field in @(
    @('issued',10780),@('ring_cursor',10820),@('ring_mask',13480),@('ring_base',13476),@('worker_lock',10812),@('worker_enabled',10816),
    @('job_callback',10828),@('job_index',10836),@('job_count',10840),
    @('job_cursor',10848),@('nesting',10860),@('active',10864),@('busy',10868),
    @('worker_spinlock',10872),@('continuation',10892),@('list',10896),@('published',10900),
    @('vblank_callback',15120),@('vblank_count',15124),@('vblank_ack',15128),@('pacing_pending',15132),@('pacing_callbacks',15136),
    @('profile_consumer',20048),@('profile_producer',20052),@('profile_enabled',20056),
    @('display_format',13232),@('display_color_space',13236)
    # 821465C8 converts a frontbuffer into capture buffers, then 821FA450
    # writes them through NtWriteFile. These are capture state, not scanout.
    @('capture_frame_index',20388),@('capture_fence',20392),@('capture_layout',20396),
    @('capture_payload_bytes',20168),@('capture_write_bytes',20172)
    @('swap_interval',13220),@('swap_flags',13456),@('swap_phase_config',11580),@('swap_profile_mode',20080)
    @('hardware_profile_query0',20060),@('hardware_profile_query1',20064),
    @('hardware_profile_query2',20068),@('hardware_profile_query3',20072),
    @('hardware_profile_wait_buffer',20076),@('hardware_profile_consumer',20084),
    @('hardware_profile_producer',20088),@('hardware_profile_cached_frame',20092)
  )) {
    $value=Read-GuestWord ($device+$field[1])
    '{0}={1} (0x{1:x8})' -f $field[0],$value
  }
  $writeback=Read-GuestWord ($device+10768)
  'fence_writeback=0x{0:x8} completed={1} cursor=0x{2:x8}' -f $writeback,(Read-GuestWord $writeback),(Read-GuestWord ($writeback+4))
  for($i=8;$i -le 16;$i+=4){'writeback+{0}={1}' -f $i,(Read-GuestWord ($writeback+$i))}
  'ring_readback={0}' -f (Read-GuestWord ($writeback+60))
  # Raw pairs of big-endian 16-bit entries, not interpreted as a gamma mode.
  foreach($offset in @(0,256,508,512,768,1020,1024,1280,1532)) {
    'default_gamma+{0}=0x{1:x8}' -f $offset,(Read-GuestWord ($device+13584+$offset))
  }
} finally {
  [void][NativeWorkerReadOnly]::CloseHandle($handle)
}
