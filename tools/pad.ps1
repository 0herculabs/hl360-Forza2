# Press buttons on the scripted pad (game started with --scripted_input=<File>).
# usage: pad.ps1 -Press A [-HoldMs 150] [-File path]
#        pad.ps1 -Press RT -HoldMs 20000          # throttle for 20 s
#        pad.ps1 -Press "RT,RIGHT" ...            # several at once
# Names: A B X Y START BACK LB RB LS RS UP DOWN LEFT RIGHT (d-pad),
#        RT LT (full trigger), SL SR SU SD (left stick left/right/up/down).
param([string]$Press, [int]$HoldMs = 150, [string]$File = (Join-Path (Split-Path $PSScriptRoot) "pad.txt"))
$bits = @{ UP = 0x0001; DOWN = 0x0002; LEFT = 0x0004; RIGHT = 0x0008; START = 0x0010; BACK = 0x0020;
           LS = 0x0040; RS = 0x0080; LB = 0x0100; RB = 0x0200; A = 0x1000; B = 0x2000; X = 0x4000; Y = 0x8000 }
$buttons = 0; $lt = 0; $rt = 0; $lx = 0; $ly = 0
foreach ($n in ($Press -split ",")) {
  switch ($n.Trim().ToUpper()) {
    "RT" { $rt = 255 } "LT" { $lt = 255 }
    "SL" { $lx = -32767 } "SR" { $lx = 32767 } "SU" { $ly = 32767 } "SD" { $ly = -32767 }
    default { $buttons = $buttons -bor $bits[$_] }
  }
}
function Write-Pad($b, $l, $r, $x, $y) {
  [IO.File]::WriteAllText($File, ("buttons={0:x} lt={1} rt={2} lx={3} ly={4} rx=0 ry=0" -f $b, $l, $r, $x, $y))
}
Write-Pad $buttons $lt $rt $lx $ly
Start-Sleep -Milliseconds $HoldMs
Write-Pad 0 0 0 0 0
Start-Sleep -Milliseconds 250
