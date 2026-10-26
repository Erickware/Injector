# ta_lib.ps1 — helpers shared by the behavioural test phases. Dot-source this file.
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
if ( -not ( Get-Command "Get-MainWindow" -ErrorAction SilentlyContinue ) )
{
    $null = Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public struct WRECT { public int Left, Top, Right, Bottom; }
public class TU {
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out WRECT r);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr e);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint att, uint to, bool yes);
    [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int vk);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumChildProc cb, IntPtr l);
    public delegate bool EnumChildProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll")] public static extern IntPtr GetWindowLongPtrW(IntPtr h, int index);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr dlg, int id);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint msg, IntPtr w, string l);
}
'@ -ReferencedAssemblies @("System.Drawing")
}

function Get-MainWindow( [string]$title )
{
    $script:hit = [IntPtr]::Zero
    $cb = [TU+EnumProc]{
        param( $h, $l )
        if ( [TU]::IsWindowVisible( $h ) )
        {
            $sb = New-Object System.Text.StringBuilder 256
            [void][TU]::GetWindowText( $h, $sb, 256 )
            if ( $sb.ToString( ) -eq $title ) { $script:hit = $h; return $false }
        }
        return $true
    }
    [void][TU]::EnumWindows( $cb, [IntPtr]::Zero )
    return $script:hit
}

function Use-App( )
{
    $script:appHwnd = Get-MainWindow "LazyUI Loader"
    if ( $script:appHwnd -eq [IntPtr]::Zero ) { throw "app window not found" }
    $r = New-Object WRECT
    [void][TU]::GetWindowRect( $script:appHwnd, [ref]$r )
    $script:ox = $r.Left; $script:oy = $r.Top
    $script:rw = $r.Right - $r.Left; $script:rh = $r.Bottom - $r.Top
    if ( $script:rw -lt 800 ) { throw "degenerate window $($script:rw)x$($script:rh)" }
    # force foreground: attach to the owner thread of the current foreground
    [void][TU]::ShowWindow( $script:appHwnd, 9 )           # SW_RESTORE
    $fgT = [TU]::GetWindowThreadProcessId( [TU]::GetForegroundWindow( ), [IntPtr]::Zero )
    $my = [TU]::GetCurrentThreadId( )
    [void][TU]::AttachThreadInput( $my, $fgT, $true )
    [void][TU]::SetForegroundWindow( $script:appHwnd )
    [void][TU]::AttachThreadInput( $my, $fgT, $false )
    Start-Sleep -Milliseconds 250
    "ui: rect=$($script:ox),$($script:oy) $($script:rw)x$($script:rh) fg-ok"
}

function Click( [int]$x, [int]$y )   # window-local
{
    [void][TU]::SetCursorPos( $script:ox + $x, $script:oy + $y )
    Start-Sleep -Milliseconds 60
    [TU]::mouse_event( 0x0002, 0, 0, 0, [UIntPtr]::Zero )
    Start-Sleep -Milliseconds 50
    [TU]::mouse_event( 0x0004, 0, 0, 0, [UIntPtr]::Zero )
    Start-Sleep -Milliseconds 150
}

function Type-Text( [string]$s )
{
    foreach ( $ch in $s.ToCharArray( ) )
    {
        [TU]::keybd_event( 0, [byte]$ch, 0x4, [UIntPtr]::Zero )
        [TU]::keybd_event( 0, [byte]$ch, (0x4 -bor 0x2), [UIntPtr]::Zero )
        Start-Sleep -Milliseconds 60
    }
}

function Press-Enter( )
{
    [TU]::keybd_event( 0x0D, 0, 0, [UIntPtr]::Zero )
    [TU]::keybd_event( 0x0D, 0, 0x2, [UIntPtr]::Zero )
    Start-Sleep -Milliseconds 150
}

function New-Capture( )
{
    $b = New-Object System.Drawing.Bitmap $script:rw, $script:rh
    $g = [System.Drawing.Graphics]::FromImage( $b )
    $g.CopyFromScreen( $script:ox, $script:oy, 0, 0, (New-Object System.Drawing.Size $script:rw, $script:rh) )
    $g.Dispose( )
    return $b
}

function Count-Where( $bmp, [int]$x0, [int]$y0, [int]$x1, [int]$y1, [scriptblock]$match )
{
    $n = 0
    for ( $y = $y0; $y -lt $y1; ++$y ) { for ( $x = $x0; $x -lt $x1; ++$x )
    {
        if ( & $match ( $bmp.GetPixel( $x, $y ) ) ) { ++$n }
    } }
    return $n
}

function Count-Green( $b, $x0, $y0, $x1, $y1 )
{
    Count-Where $b $x0 $y0 $x1 $y1 { param($p) $p.G -gt 170 -and $p.R -lt 120 -and $p.B -gt 90 -and $p.B -lt 175 }
}
function Count-Red( $b, $x0, $y0, $x1, $y1 )
{
    Count-Where $b $x0 $y0 $x1 $y1 { param($p) $p.R -gt 220 -and $p.G -lt 130 -and $p.B -lt 130 }
}
function Count-Amber( $b, $x0, $y0, $x1, $y1 )
{
    Count-Where $b $x0 $y0 $x1 $y1 { param($p) $p.R -gt 210 -and $p.G -gt 150 -and $p.B -lt 90 }
}
function Count-Accent( $b, $x0, $y0, $x1, $y1 ) # current icon_blue family (strong blue channel)
{
    Count-Where $b $x0 $y0 $x1 $y1 { param($p) $p.B -gt 200 -and $p.G -gt 70 -and $p.G -lt 170 -and $p.R -lt 90 }
}
function Count-Text( $b, $x0, $y0, $x1, $y1 )  # any clearly lit glyph pixel
{
    Count-Where $b $x0 $y0 $x1 $y1 { param($p) $p.R + $p.G + $p.B -gt 210 }
}

function Close-Marker( )
{
    $d0 = [TU]::FindWindow( $null, "INJTEST-MARKER" )
    if ( $d0 -ne [IntPtr]::Zero )
    {
        $ok = [TU]::FindWindowEx( $d0, [IntPtr]::Zero, "Button", "OK" )
        [void][TU]::SendMessageW( $ok, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero )
        Start-Sleep -Milliseconds 400
        return $true
    }
    return $false
}

function Read-InjectLog( )
{
    $p = "$PSScriptRoot\..\build\Release\injector.log"
    if ( Test-Path $p ) { return ( Get-Content $p -Tail 60 ) }
    return @( )
}
