# Interleaved timing series: for each round, each variant (out/exp/<variant>) on each scenario.
#   tools/codegen-fork/time-series.ps1 -Variants base,ipa-exact,ipa-abi -Rounds 3 -Start 1
param([string[]]$Variants = @('base','ipa-exact','ipa-abi'), [int]$Rounds = 3, [int]$Start = 1,
      [string[]]$Scenarios = @('benchmark','horde-ants-1000'))
$here = $PSScriptRoot
for ($r = $Start; $r -lt $Start + $Rounds; $r++) {
  foreach ($sc in $Scenarios) {
    foreach ($v in $Variants) {
      $short = if ($sc -like 'horde*') { 'horde' } else { 'm1' }
      & "$here\run-timed.ps1" -Exe $v -Scenario $sc -Tag "t$r-$v-$short"
    }
  }
}
