# tg_inject.ps1 — scripted end-to-end injection gauntlet runner.
# Drives the REAL app via its own script channel (INJECTOR_CAPTURE_SCRIPT): clicks,
# text and even the OS file dialog are filled with SetDlgItemText — no luck-based
# keyboard focus. Evidence: injector.log lines, the INJTEST-MARKER window inside
# the target, and pixel zones in the captured frame.
param(
    [int]$Method = 0,                 # 0 LoadLibrary, 1 Manual Map, 2 Thread Hijack
    [string]$Target = "notepad",
    [switch]$NoDll,
    [string]$DllPath = "$PSScriptRoot\injtest.dll",
    [string]$Wow64 = "",              # set to a SysWOW64 exe to spawn a 32-bit target
    [string]$Shot = "$PSScriptRoot\shot_gauntlet.png"
)
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\ta_lib.ps1"

$logPath = "$PSScriptRoot\..\build\Release\injector.log"

# ---- world -----------------------------------------------------------------
Get-Process Injector -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process notepad -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 700
Remove-Item $logPath -ErrorAction SilentlyContinue
Remove-Item $Shot -ErrorAction SilentlyContinue

$np = if ( $Wow64 -ne "" ) { Start-Process $Wow64 -PassThru } else { Start-Process notepad -PassThru }
Start-Sleep -Seconds 2
"d000 target=$($np.ProcessName) pid=$($np.Id)"

# ---- script -----------------------------------------------------------------
$steps = [System.Collections.Generic.List[string]]::new( )
if ( $Method -ne 0 )
{
    $itemY = 181 + 30 * $Method     # Manual Map -> 211, Thread Hijack -> 241
    $steps.Add( "move 92 149" );  $steps.Add( "down" ); $steps.Add( "up" ); $steps.Add( "wait 50" )
    $steps.Add( "move 782 145" ); $steps.Add( "down" ); $steps.Add( "up" ); $steps.Add( "wait 40" )
    $steps.Add( "move 782 $itemY" ); $steps.Add( "down" ); $steps.Add( "up" ); $steps.Add( "wait 30" )
    $steps.Add( "move 92 107" );  $steps.Add( "down" ); $steps.Add( "up" ); $steps.Add( "wait 40" )
}
$steps.Add( "move 300 385" ); $steps.Add( "down" ); $steps.Add( "up" )
$steps.Add( "type $Target" ); $steps.Add( "wait 150" )
$steps.Add( "move 380 506" ); $steps.Add( "down" ); $steps.Add( "up" ); $steps.Add( "wait 20" )
if ( -not $NoDll )
{
    $steps.Add( "move 307 263" ); $steps.Add( "down" ); $steps.Add( "up" ); $steps.Add( "wait 300" )
}
$steps.Add( "move 492 263" ); $steps.Add( "down" ); $steps.Add( "up" ); $steps.Add( "wait 45" )
$steps.Add( "move 492 263" ); $steps.Add( "down" ); $steps.Add( "up" ); $steps.Add( "wait 45" )
$steps.Add( "move 492 263" ); $steps.Add( "down" ); $steps.Add( "up" )
$steps.Add( "wait 240" )
$steps.Add( "shot $Shot" )
$ks = [string]::Join( "; ", $steps )
"d010 script=$ks"

# ---- run ---------------------------------------------------------------------
$env:INJECTOR_CAPTURE_SCRIPT = $ks
$app = Start-Process "$PSScriptRoot\..\build\Release\Injector.exe" -PassThru
$env:INJECTOR_CAPTURE_SCRIPT = $null

# ---- dialog companion: fills the OFN file name box when Browse opens ---------
$appHwnd = [IntPtr]::Zero
foreach ( $try in 1..40 )
{
    $finder = [TU+EnumProc]{
        param( $h, $l )
        $pidOut = [uint32]0
        [void][TU]::GetWindowThreadProcessId( $h, [ref]$pidOut )
        if ( $pidOut -eq $app.Id )
        {
            $cls = New-Object System.Text.StringBuilder 64
            [void][TU]::GetClassNameW( $h, $cls, 64 )
            if ( $cls.ToString( ) -eq "InjectorWindow" ) { $script:aHit = $h; return $false }
        }
        return $true
    }
    $script:aHit = [IntPtr]::Zero
    [void][TU]::EnumWindows( $finder, [IntPtr]::Zero )
    if ( $script:aHit -ne [IntPtr]::Zero ) { $appHwnd = $script:aHit; break }
    Start-Sleep -Milliseconds 150
}
"d015 appHwnd=$appHwnd"
$d015 = $appHwnd -ne [IntPtr]::Zero

# Park the REAL mouse cursor outside the app's rect: after the modal OFN closes,
# Windows feeds the visible app a WM_MOUSEMOVE per frame from the physical
# cursor, which overwrites every scripted mouse position (measured: all three
# inject clicks landed at the user cursor's rest point instead of the button).
if ( $d015 )
{
    $r = New-Object WRECT
    [void][TU]::GetWindowRect( $appHwnd, [ref]$r )
    if ( -not ( "W32.TK3" -as [type] ) )
    {
        Add-Type -MemberDefinition '[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);' -Name TK3 -Namespace W32
    }
    [void][W32.TK3]::SetCursorPos( $r.Right + 90, $r.Bottom + 90 )
    "d016 cursorParked=($($r.Right + 90),$($r.Bottom + 90))"
}

$ofnFilled = $false
if ( -not $NoDll )
{
    Start-Sleep -Milliseconds 900     # browse click lands ~1.5s into the script
    $dlg = [IntPtr]::Zero
    foreach ( $try in 1..80 )
    {
        $cb = [TU+EnumProc]{
            param( $h, $l )
            $parent = [TU]::GetWindowLongPtrW( $h, -8 )   # GWLP_HWNDPARENT
            $cls = New-Object System.Text.StringBuilder 64
            [void][TU]::GetClassNameW( $h, $cls, 64 )
            if ( $parent -eq $appHwnd -and $cls.ToString( ) -eq "#32770" ) { $script:dHit = $h; return $false }
            return $true
        }
        $script:dHit = [IntPtr]::Zero
        [void][TU]::EnumWindows( $cb, [IntPtr]::Zero )
        if ( $script:dHit -ne [IntPtr]::Zero ) { $dlg = $script:dHit; break }
        Start-Sleep -Milliseconds 200
    }
    if ( $dlg -ne [IntPtr]::Zero )
    {
        $ecb = [TU+EnumChildProc]{
            param( $h, $l )
            $cls = New-Object System.Text.StringBuilder 64
            [void][TU]::GetClassNameW( $h, $cls, 64 )
            if ( $cls.ToString( ) -eq "Edit" ) { $script:eHit = $h; return $false }
            return $true
        }
        $script:eHit = [IntPtr]::Zero
        [void][TU]::EnumChildWindows( $dlg, $ecb, [IntPtr]::Zero )
        if ( $script:eHit -ne [IntPtr]::Zero )
        {
            [void][TU]::SendMessageW( $script:eHit, 0x000C, [IntPtr]::Zero, $DllPath )   # WM_SETTEXT
            $ok = [TU]::GetDlgItem( $dlg, 1 )
            if ( $ok -ne [IntPtr]::Zero )
            {
                [void][TU]::SendMessageW( $ok, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero )  # BM_CLICK
            }
            $ofnFilled = $true
        }
    }
    "d020 ofnFilled=$ofnFilled"
}
# The modal OFN activates the app; from then on the ImGui Win32 backend polls the
# PHYSICAL cursor every frame and overwrites scripted mouse positions (measured:
# all inject clicks landed at the physical cursor). Park the physical cursor on
# the Inject button (492,263 in client coords) so the polled position IS the
# button and the scripted down/up lands there instead.
if ( -not $NoDll -and $d015 )
{
    $r2 = New-Object WRECT
    [void][TU]::GetWindowRect( $appHwnd, [ref]$r2 )
    [void][W32.TK3]::SetCursorPos( $r2.Left + 492, $r2.Top + 263 )
    "d021 cursorOnInject=($($r2.Left + 492),$($r2.Top + 263))"
}
$app.WaitForExit( )
"d030 appExit=$($app.ExitCode)"

Start-Sleep -Milliseconds 700

# ---- evidence -----------------------------------------------------------------
"--- injector.log ---"
if ( Test-Path $logPath ) { Get-Content $logPath } else { "(no log file)" }

$marker = [TU]::FindWindow( $null, "INJTEST-MARKER" )
$d050 = $marker -ne [IntPtr]::Zero
$dismissed = Close-Marker
"d050 markerFound=$d050 dismissed=$dismissed"

$b = New-Object System.Drawing.Bitmap( $Shot )
$logGreen = Count-Green $b 623 340 866 470
$logRed   = Count-Red   $b 623 340 866 470
$logAmber = Count-Amber $b 623 340 866 470
$toastLedG = Count-Green $b 616 20 622 84
$toastLedR = Count-Red   $b 616 20 622 84
"d090 logGreen=$logGreen logRed=$logRed logAmber=$logAmber toastG=$toastLedG toastR=$toastLedR"

switch ( $Method ) { 0 { $want = "LoadLibrary" } 1 { $want = "Manual Map" } 2 { $want = "Thread Hijack" } }

$pass = $false
if ( $NoDll )
{
    $pass = ( $logRed -gt 0 ) -and ( -not $d050 )
}
elseif ( $Wow64 -ne "" )
{
    $pass = ( $logRed -gt 0 ) -and ( -not $d050 )
}
else
{
    $pass = $d050 -and ( $logGreen -gt 0 )
}
if ( $pass ) { "PASS method=$want" } else { "FAIL method=$want"; exit 1 }
