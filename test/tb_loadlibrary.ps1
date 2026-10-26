# tb_loadlibrary.ps1 — Phase B: real LoadLibrary E2E with foreground-forced driving.
. "$PSScriptRoot\ta_lib.ps1"

Get-Process Injector,notepad -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 700
$np = Start-Process notepad -PassThru
Start-Sleep -Seconds 2
"d000 notepad pid=$($np.Id)"
Start-Process "$PSScriptRoot\..\build\Release\Injector.exe"
Start-Sleep -Milliseconds 2500
Use-App

Click 300 385            # focus Search Process
Type-Text "notepad"
Start-Sleep -Milliseconds 900
$s0 = New-Capture
$s0.Save( "$PSScriptRoot\shot_search.png" )
"t010 searchZoneText=$(Count-Text $s0 218 374 396 398)"

Click 380 506            # select row 0
Start-Sleep -Milliseconds 400
Click 307 263            # Browse -> OFN
Start-Sleep -Seconds 1
Type-Text "$PSScriptRoot\injtest.dll"
Press-Enter
Start-Sleep -Milliseconds 900

$b1 = New-Capture
$g1 = Count-Green $b1 623 340 866 420
$r1 = Count-Red   $b1 623 340 866 420
Click 492 263            # Inject
Start-Sleep -Seconds 2
$b2 = New-Capture
$b2.Save( "$PSScriptRoot\shot_after_loadlibrary.png" )
$g2 = Count-Green $b2 623 340 866 420
$r2 = Count-Red   $b2 623 340 866 420
$t2 = Count-Green $b2 616 20 622 84
$d0 = [TU]::FindWindow( $null, "INJTEST-MARKER" )
"d100 logGreen=$g1->$g2 logRed=$r1->$r2 toastLedGreen=$t2 markerFound=$($d0 -ne [IntPtr]::Zero)"
$dismissed = Close-Marker
"d101 markerDismissed=$dismissed"
"--- injector.log ---"
Read-InjectLog | Select-Object -Last 12

if ( $d0 -eq [IntPtr]::Zero ) { "FAIL: marker never appeared"; exit 1 }
if ( $g2 -le $g1 )          { "FAIL: no green success row";   exit 1 }
"PASS: loadlibrary E2E"
