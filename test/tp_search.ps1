# tp_search.ps1 — does keybd_event UNICODE typing reach the app's search field?
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
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
}
'@
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
Get-Process Injector -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 600
Start-Process "$PSScriptRoot\..\build\Release\Injector.exe"
Start-Sleep -Milliseconds 2200
$hw = Get-MainWindow "Injector"
$r = New-Object WRECT; [void][TU]::GetWindowRect( $hw, [ref]$r )
$ox = $r.Left; $oy = $r.Top
"win: $($r.Right-$r.Left)x$($r.Bottom-$r.Top) at $ox,$oy"

function Click( [int]$x, [int]$y )
{
    [void][TU]::SetCursorPos( $ox + $x, $oy + $y )
    Start-Sleep -Milliseconds 60
    [TU]::mouse_event( 0x0002, 0, 0, 0, [UIntPtr]::Zero ); Start-Sleep -Milliseconds 50
    [TU]::mouse_event( 0x0004, 0, 0, 0, [UIntPtr]::Zero ); Start-Sleep -Milliseconds 150
}
function Sample-Capture( )
{
    $b = New-Object System.Drawing.Bitmap 200, 24
    $g = [System.Drawing.Graphics]::FromImage( $b )
    $g.CopyFromScreen( $ox + 218, $oy + 374, 0, 0, (New-Object System.Drawing.Size 200, 24) )
    $g.Dispose( ); $b
}
function Bright( $b ) # pixels clearly brighter than the input fill (~ rgb 30-ish)
{
    $n = 0
    for ( $y = 4; $y -lt 22; ++$y ) { for ( $x = 2; $x -lt 198; ++$x )
    {
        $p = $b.GetPixel( $x, $y ); if ( $p.R + $p.G + $p.B -gt 200 ) { ++$n }
    } }
    $n
}
$b0 = Sample-Capture; $n0 = Bright $b0
Click 300 385
foreach ( $ch in "notepad".ToCharArray( ) )
{
    [TU]::keybd_event( 0, [byte]$ch, 0x4, [UIntPtr]::Zero )
    [TU]::keybd_event( 0, [byte]$ch, (0x4 -bor 0x2), [UIntPtr]::Zero )
    Start-Sleep -Milliseconds 150
}
Start-Sleep -Milliseconds 800
$b1 = Sample-Capture; $n1 = Bright $b1
"search field bright pixels: $n0 -> $n1 (typed 'notepad')"
