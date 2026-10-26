# td_iso.ps1 — isolation probes: A) row click alone, B) search typing alone.
$ErrorActionPreference = "Stop"
. "C:\Users\Null!\Downloads\DLL-Injector\test\ta_lib.ps1"

function Run-Phase( [string]$ks, [string]$name )
{
    Get-Process LazyUI -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
    $out = "C:\Users\Null!\Downloads\DLL-Injector\test\shot_iso_$name.png"
    Remove-Item $out -ErrorAction SilentlyContinue
    $env:LAZYUI_CAPTURE_SCRIPT = "$ks; shot $out"
    Start-Process "C:\Users\Null!\Downloads\DLL-Injector\build\Release\LazyUI.exe" -Wait
    $env:LAZYUI_CAPTURE_SCRIPT = $null
    Write-Host "phase $name exit=$LASTEXITCODE"
    return $out
}

$A = Run-Phase "wait 40; move 380 506; down; up; wait 30" "row"
$bA = New-Object System.Drawing.Bitmap( $A )
"iso row: status_target lit = $(Count-Text $bA 700 200 866 216)"

$B = Run-Phase "wait 40; move 300 385; down; up; wait 5; type notepad; wait 60" "search"
$bB = New-Object System.Drawing.Bitmap( $B )
"iso search: search_box lit = $(Count-Text $bB 218 374 396 398)"
