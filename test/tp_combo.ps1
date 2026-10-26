# tp_combo.ps1 — calibrate the Settings > Method combo field center via scripted clicks.
param( [int]$Y = 142 )
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\ta_lib.ps1"

$out = "$PSScriptRoot\probe_combo_$Y.png"
Remove-Item $out -ErrorAction SilentlyContinue

Get-Process LazyUI -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

$script = "move 92 149; down; up; wait 60; move 782 $Y; down; up; wait 45; shot $out"
$env:LAZYUI_CAPTURE_SCRIPT = $script
Start-Process "$PSScriptRoot\..\build\Release\LazyUI.exe" -Wait
$env:LAZYUI_CAPTURE_SCRIPT = $null
"exit=$LASTEXITCODE"

$b = New-Object System.Drawing.Bitmap( $out )
# popup body: surface_2 (11,20,35), fills the option list below the field
$n = Count-Where $b 600 186 880 290 { param($p) $p.R -eq 11 -and $p.G -eq 20 -and $p.B -eq 35 }
"fill surface_2 in popup zone: $n"
