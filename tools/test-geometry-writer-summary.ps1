$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$result=& "$PSScriptRoot/summarize-geometry-writer-sites.ps1" -LogPath "$root/tests/fixtures/geometry-writer-sites.log.txt" -Inventory "$root/tests/fixtures/geometry-writer-sites.csv"
if($result.Sites.Count -ne 3 -or $result.OmittedHits -ne 7 -or $result.UnresolvedSites -ne 1) {throw 'Writer summary coverage mismatch'}
$first=@($result.Sites | Where-Object Lifetime -EQ 1)
if($first.Count -ne 1 -or $first[0].Calls -ne 5 -or $first[0].Function -ne 'sub_82100000') {throw 'Writer summary deduplication or attribution mismatch'}
if(@($result.Sites | Where-Object Lifetime -EQ 2).Count -ne 1) {throw 'Writer summary merged reused owner lifetime'}
'Geometry writer summary fixture passed'
