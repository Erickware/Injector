# td_shot.ps1 — zone probes over a saved frame: which UI state made it visible?
param( [string]$Shot = "$PSScriptRoot\shot_ll.png" )
$ErrorActionPreference = "Stop"
. "$PSScriptRoot\ta_lib.ps1"

$b = New-Object System.Drawing.Bitmap( $Shot )
# window-local zones of interest
$zones = @(
    @{ name = "dll_path_box";   x0 = 218; y0 = 196; x1 = 581; y1 = 226 },
    @{ name = "log_rows";       x0 = 623; y0 = 340; x1 = 866; y1 = 470 },
    @{ name = "status_values";  x0 = 700; y0 = 126; x1 = 866; y1 = 260 },
    @{ name = "search_box";     x0 = 218; y0 = 374; x1 = 396; y1 = 398 },
    @{ name = "row0_name";      x0 = 218; y0 = 490; x1 = 500; y1 = 520 },
    @{ name = "inject_button";  x0 = 404; y0 = 247; x1 = 581; y1 = 279 }
)
foreach ( $z in $zones )
{
    $t = Count-Text $b $z.x0 $z.y0 $z.x1 $z.y1
    "zone $($z.name): lit=$t"
}
# block colour sampling: exact fills say which widget part exists there
$samples = @(
    @{ name = "inject_btn_fill"; x = 492; y = 263 },   # button_2 base = icon_blue
    @{ name = "row0_bg";         x = 300; y = 490 }
)
foreach ( $s in $samples )
{
    $p = $b.GetPixel( $s.x, $s.y )
    "sample $($s.name): R=$($p.R) G=$($p.G) B=$($p.B) A=$($p.A)"
}
