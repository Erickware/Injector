# td_flow.ps1 — minimal scripted flow, one probe at the end:
#   click search -> type -> click row0 -> shot
# Then report lit-pixel counts in the search field / status target / row0 name.
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\ta_lib.ps1"

Get-Process LazyUI -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
$out = "$PSScriptRoot\shot_flow.png"
Remove-Item $out -ErrorAction SilentlyContinue

$ks = "move 300 385; down; up; type notepad; wait 150; move 380 506; down; up; wait 30; shot $out"
$env:LAZYUI_CAPTURE_SCRIPT = $ks
Start-Process "$PSScriptRoot\..\build\Release\LazyUI.exe" -Wait
$env:LAZYUI_CAPTURE_SCRIPT = $null
"exit=$LASTEXITCODE"

$b = New-Object System.Drawing.Bitmap( $out )
"search_box lit: $(Count-Text $b 218 374 396 398)"
"status_target lit: $(Count-Text $b 700 200 866 216)"
"row0_name lit: $(Count-Text $b 218 488 500 522)"
