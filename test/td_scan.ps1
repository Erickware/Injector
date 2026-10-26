# td_scan.ps1 — column scan at x=300 over a saved frame: find the real y-ranges
# of the search box fill and of row 0, instead of trusting derived math.
param( [string]$Shot = "$PSScriptRoot\shot_iso_search.png" )
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$b = New-Object System.Drawing.Bitmap( $Shot )

function Tag( $p )
{
    if ( $p.R -eq 11 -and $p.G -eq 20 -and $p.B -eq 35 ) { return "S2" }   # surface_2
    if ( $p.R -eq 21 -and $p.G -eq 34 -and $p.B -eq 56 ) { return "AC" }   # accent border
    if ( $p.R -eq 8  -and $p.G -eq 14 -and $p.B -eq 25 ) { return "S1" }   # surface
    if ( $p.R -eq 23 -and $p.G -eq 31 -and $p.B -eq 45 ) { return "FB" }   # fill_blue_2-ish
    if ( $p.R + $p.G + $p.B -gt 640 )                  { return "TXT" }  # join glyph words
        return "."
}

$prev = ""
$start = 0
for ( $y = 320; $y -lt 560; ++$y )
{
    $t = Tag ( $b.GetPixel( 300, $y ) )
    if ( $t -ne $prev )
    {
        if ( $prev -ne "" ) { Write-Host ("{0,4}..{1,4}: {2}" -f $start, ($y - 1), $prev) }
        $start = $y; $prev = $t
    }
}
Write-Host ("{0,4}..{1,4}: {2}" -f $start, 559, $prev)
