# td_realio.ps1 — REAL OS input + io dump correlation: the decisive cursor.
$ErrorActionPreference = "Stop"
. "C:\Users\Null!\Downloads\DLL-Injector\test\ta_lib.ps1"
Add-Type -AssemblyName System.Windows.Forms

Get-Process LazyUI -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 600
$dbg = "C:\Users\Null!\Downloads\DLL-Injector\test\io_real.txt"
Remove-Item $dbg -ErrorAction SilentlyContinue

$env:LAZYUI_DEBUG_IO = $dbg
$app = Start-Process "C:\Users\Null!\Downloads\DLL-Injector\build\Release\LazyUI.exe" -PassThru
$env:LAZYUI_DEBUG_IO = $null
Start-Sleep -Seconds 4
$hwnd = Get-MainWindow "LazyUI Loader"
[void]( Use-App $hwnd | Click 300 385 )
Start-Sleep -Milliseconds 500
[System.Windows.Forms.SendKeys]::SendWait( "notepad" )
Start-Sleep -Seconds 3
Stop-Process -Id $app.Id -Force

Get-Content $dbg | Where-Object { $_ -match "clicked=1|queueChars=[1-9]|anyItemActive=1|wantText=1" } | Select-Object -First 20
"=== tail around typing ==="
Get-Content $dbg | Select-Object -Last 6
