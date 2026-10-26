# td_io.ps1 — capture io-level truth for the failing search activation.
$ErrorActionPreference = "Stop"

Get-Process LazyUI -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

$dbg = "C:\Users\Null!\Downloads\DLL-Injector\test\io_debug.txt"
$shot = "C:\Users\Null!\Downloads\DLL-Injector\test\shot_io.png"
Remove-Item $dbg, $shot -ErrorAction SilentlyContinue

$ks = "wait 40; move 300 385; down; up; wait 5; type notepad; wait 60; move 380 506; down; up; wait 30; shot $shot"
$env:LAZYUI_DEBUG_IO = $dbg
$env:LAZYUI_CAPTURE_SCRIPT = $ks
Start-Process "C:\Users\Null!\Downloads\DLL-Injector\build\Release\LazyUI.exe" -Wait
$env:LAZYUI_DEBUG_IO = $null
$env:LAZYUI_CAPTURE_SCRIPT = $null

Get-Content $dbg | Select-Object -First 130
