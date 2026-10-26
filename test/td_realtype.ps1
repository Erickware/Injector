# td_realtype.ps1 — REAL input path sanity test: visible window, foreground,
# OS-level mouse click, SendInput with virtual-key scancodes (no synthetic io).
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\ta_lib.ps1"

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class TK2
{
    [StructLayout(LayoutKind.Explicit)]
    public struct INPUTUNION { [FieldOffset(0)] public KEYBDINPUT ki; [FieldOffset(0)] public MOUSEINPUT mi; }
    [StructLayout(LayoutKind.Sequential)]
    public struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    public struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    public struct INPUT { public uint type; public INPUTUNION u; }
    [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] p, int cb);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
    [DllImport("user32.dll")] public static extern short VkKeyScanW(ushort ch);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
}
"@

Get-Process LazyUI -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 600
$app = Start-Process "$PSScriptRoot\..\build\Release\LazyUI.exe" -PassThru
Start-Sleep -Seconds 4
$hwnd = Get-MainWindow "LazyUI Loader"
"hwnd=$hwnd"
[void]( Use-App $hwnd | Click 300 385 )

$r = New-Object TK2+RECT
[void][TK2]::GetWindowRect( $hwnd, [ref]$r )
"rect=($($r.l),$($r.t),$($r.r),$($r.b))"

# baseline: rows zone lit + search zone lit (screen capture of the app area)
function Shot-Zone( [int]$x, [int]$y, [int]$w, [int]$h )
{
    $bmp = New-Object System.Drawing.Bitmap( $w, $h )
    $g = [System.Drawing.Graphics]::FromImage( $bmp )
    $g.CopyFromScreen( $x, $y, 0, 0, $bmp.Size )
    $g.Dispose( )
    return $bmp
}
$b0 = Shot-Zone ( $r.l + 204 ) ( $r.t + 480 ) 240 100
"baseline rows lit = $(Count-Text $b0 0 0 240 100)"

# type "notepad" via real virtual keys
foreach ( $ch in "notepad".ToCharArray( ) )
{
    $vk = [TK2]::VkKeyScanW( [System.Convert]::ToUInt16( $ch ) )
    $bind = ( $vk -band 0xFF )
    $down = New-Object TK2+INPUT
    $down.type = 1; $down.u.ki.wVk = [uint16]$bind; $down.u.ki.dwFlags = 0
    $up = New-Object TK2+INPUT
    $up.type = 1; $up.u.ki.wVk = [uint16]$bind; $up.u.ki.dwFlags = 2
    [void][TK2]::SendInput( 1, @( $down ), [System.Runtime.InteropServices.Marshal]::SizeOf( [type][TK2+INPUT] ) )
    Start-Sleep -Milliseconds 30
    [void][TK2]::SendInput( 1, @( $up ), [System.Runtime.InteropServices.Marshal]::SizeOf( [type][TK2+INPUT] ) )
    Start-Sleep -Milliseconds 40
}
Start-Sleep -Seconds 3

$b1 = Shot-Zone ( $r.l + 204 ) ( $r.t + 480 ) 240 100
"after-type rows lit = $(Count-Text $b1 0 0 240 100)"
$b2 = Shot-Zone ( $r.l + 218 ) ( $r.t + 374 ) 178 36
"search box lit = $(Count-Text $b2 0 0 178 36)"
