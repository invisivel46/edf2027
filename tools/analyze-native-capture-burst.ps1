param([Parameter(Mandatory=$true)][string]$Prefix)
$ErrorActionPreference='Stop'
Add-Type @'
using System;
using System.IO;
public static class NativeBurstPixels {
  public static long[] Scan(string path,string previous) {
    var data=File.ReadAllBytes(path);
    int w=BitConverter.ToInt32(data,18),h=-BitConverter.ToInt32(data,22);
    int offset=BitConverter.ToInt32(data,10),pitch=(w*3+3)&~3;
    if(w<=0 || h<=0 || data[0]!=66 || data[1]!=77 || BitConverter.ToInt16(data,28)!=24 ||
       (long)offset+(long)pitch*h!=data.Length) throw new Exception("Unsupported capture BMP: "+path);
    byte[] old=String.IsNullOrEmpty(previous)?null:File.ReadAllBytes(previous);
    if(old!=null && (old.Length!=data.Length || BitConverter.ToInt32(old,18)!=w ||
       BitConverter.ToInt32(old,22)!=-h || BitConverter.ToInt32(old,10)!=offset))
      throw new Exception("Capture dimensions changed");
    long black=0,invalid=0,darkened=0;
    int xmin=w,ymin=h,xmax=-1,ymax=-1,invalidX=-1,invalidY=-1;
    for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
      int i=offset+y*pitch+x*3;
      if(data[i]==255 && data[i+1]==0 && data[i+2]==255) {invalid++;invalidX=x;invalidY=y;}
      if(data[i]<8 && data[i+1]<8 && data[i+2]<8) {
        black++;
        if(old!=null && old[i]+old[i+1]+old[i+2]>300) {
          darkened++; xmin=Math.Min(xmin,x); ymin=Math.Min(ymin,y);
          xmax=Math.Max(xmax,x); ymax=Math.Max(ymax,y);
        }
      }
    }
    return new long[]{black,invalid,darkened,xmin,ymin,xmax,ymax,invalidX,invalidY};
  }
}
'@
foreach($kind in @('output','scene-color')) {
  $directory=Split-Path ([IO.Path]::GetFullPath($Prefix))
  $name=Split-Path $Prefix -Leaf
  $files=@(Get-ChildItem -LiteralPath $directory -Filter "$name.$kind.*.bmp" |
    Sort-Object { [long]($_.BaseName.Split('.')[-1]) })
  $previous=$null
  $rows=foreach($file in $files) {
    $stats=[NativeBurstPixels]::Scan($file.FullName,$previous)
    [pscustomobject]@{Kind=$kind;Frame=[long]($file.BaseName.Split('.')[-1]);Black=$stats[0];
      Magenta=$stats[1];BecameBlack=$stats[2];Bounds=($stats[3..6] -join ',');InvalidXY=($stats[7..8] -join ',')}
    $previous=$file.FullName
  }
  "$kind captures: $($files.Count); newly black pixels are candidates, not proof of a rendering defect"
  $rows | Sort-Object BecameBlack -Descending | Select-Object -First 12 | Format-Table -AutoSize
  $rows | Where-Object Magenta -gt 0 | Select-Object -First 5 | Format-Table -AutoSize
}
