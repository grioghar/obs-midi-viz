<#
.SYNOPSIS
  Generates obs-midi-viz mockup SVG images into docs/images/.
  Run from the repo root: powershell -File docs/render/gen-mockups.ps1
#>

$OutDir = "$PSScriptRoot\..\images"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function fmt($v) { return [string]::Format('{0:F2}', $v) }

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# helpers
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
function isWhite($n) { @(0,2,4,5,7,9,11) -contains ($n % 12) }

function lerpHex($a, $b, $t) {
    $ah = $a.TrimStart('#')
    $bh = $b.TrimStart('#')
    $ar=[Convert]::ToInt32($ah.Substring(0,2),16); $br=[Convert]::ToInt32($bh.Substring(0,2),16)
    $ag=[Convert]::ToInt32($ah.Substring(2,2),16); $bg=[Convert]::ToInt32($bh.Substring(2,2),16)
    $ab=[Convert]::ToInt32($ah.Substring(4,2),16); $bb=[Convert]::ToInt32($bh.Substring(4,2),16)
    $r=[int]($ar+($br-$ar)*$t); $g=[int]($ag+($bg-$ag)*$t); $bl=[int]($ab+($bb-$ab)*$t)
    return '#{0:X2}{1:X2}{2:X2}' -f $r,$g,$bl
}

function hexLum($hex) {
    $h=$hex.TrimStart('#')
    $r=[Convert]::ToInt32($h.Substring(0,2),16)/255
    $g=[Convert]::ToInt32($h.Substring(2,2),16)/255
    $b=[Convert]::ToInt32($h.Substring(4,2),16)/255
    return 0.299*$r + 0.587*$g + 0.114*$b
}

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# 1. PIANO ROLL   960 Ã— 460
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
$W=960; $H=460; $kbH=200; $kbY=$H-$kbH

# Build note x-positions
$wIdx=@{}; $wi=0
for($i=0;$i-lt 128;$i++){ if(isWhite $i){ $wIdx[$i]=$wi; $wi++ } }
$wCount=$wi   # 75
$wkW=[double]$W/$wCount   # 12.8
$bkW=$wkW*0.63
$bkH=$kbH*0.61
$noteX=@{}
for($i=0;$i-lt 128;$i++){
    if(isWhite $i){ $noteX[$i]=[double]$wIdx[$i]*$wkW }
    else{ $noteX[$i]=$noteX[$i-1]+$wkW-$bkW/2 }
}

$sb=[System.Text.StringBuilder]::new()
$null=$sb.AppendLine("<?xml version='1.0' encoding='UTF-8'?>")
$null=$sb.AppendLine("<svg width='$W' height='$H' viewBox='0 0 $W $H' xmlns='http://www.w3.org/2000/svg'>")
$null=$sb.AppendLine(@"
<defs>
  <linearGradient id='wfBg' x1='0' y1='0' x2='0' y2='1'>
    <stop offset='0' stop-color='#050512'/>
    <stop offset='1' stop-color='#0a0a1e'/>
  </linearGradient>
  <linearGradient id='glowLine' x1='0' y1='0' x2='0' y2='1'>
    <stop offset='0' stop-color='rgba(80,140,255,0)'/>
    <stop offset='0.5' stop-color='rgba(80,140,255,0.55)'/>
    <stop offset='1' stop-color='rgba(80,140,255,0)'/>
  </linearGradient>
  <linearGradient id='wkShad' x1='0' y1='0' x2='0' y2='1'>
    <stop offset='0.75' stop-color='rgba(0,0,0,0)'/>
    <stop offset='1'    stop-color='rgba(0,0,0,0.28)'/>
  </linearGradient>
</defs>
"@)
$null=$sb.AppendLine("<rect width='$W' height='$H' fill='#06060f'/>")
$null=$sb.AppendLine("<rect width='$W' height='$kbY' fill='url(#wfBg)'/>")

# Octave separator lines in waterfall
for($i=0;$i-lt 128;$i++){
    if(($i % 12) -eq 0){
        $x=fmt($noteX[$i])
        $null=$sb.AppendLine("<line x1='$x' y1='0' x2='$x' y2='$kbY' stroke='rgba(70,70,140,0.10)' stroke-width='0.5'/>")
    }
}

# Waterfall note blocks  [note, yEnd (from top), height, fill, opacity]
$blocks = @(
    # --- Controller 1 (blue) C-major chord fragments, various ages ---
    @(60,$kbY-20,20,'#44aaff',0.95),  @(64,$kbY-16,16,'#44aaff',0.95),  @(67,$kbY-12,12,'#44aaff',0.95),
    @(60,$kbY-70,30,'#2277cc',0.82),  @(64,$kbY-76,26,'#2277cc',0.82),  @(67,$kbY-72,20,'#2277cc',0.82),
    @(60,$kbY-140,36,'#44aaff',0.65), @(62,$kbY-155,24,'#3399ee',0.58), @(65,$kbY-130,40,'#44aaff',0.65),
    @(69,$kbY-175,28,'#2277cc',0.52), @(71,$kbY-198,46,'#44aaff',0.50), @(72,$kbY-225,32,'#44aaff',0.44),
    @(74,$kbY-255,28,'#2277cc',0.40), @(55,$kbY-268,38,'#44aaff',0.38), @(48,$kbY-298,52,'#44aaff',0.34),
    # --- Controller 2 (red) Am / F chord fragments ---
    @(57,$kbY-26,20,'#ff4444',0.95),  @(60,$kbY-42,22,'#bb55ee',0.90),
    @(52,$kbY-88,34,'#ff4444',0.78),  @(55,$kbY-105,26,'#cc2222',0.72),
    @(59,$kbY-138,38,'#ff4444',0.62), @(57,$kbY-198,52,'#cc2222',0.52),
    @(64,$kbY-118,24,'#ee4455',0.68), @(76,$kbY-168,36,'#ff4444',0.56),
    @(72,$kbY-238,40,'#cc2244',0.44), @(69,$kbY-278,28,'#ff4444',0.38)
)

foreach($b in $blocks){
    $note=$b[0]; $y=[double]$b[1]; $bh=[double]$b[2]; $fill=$b[3]; $op=$b[4]
    $bx=fmt($noteX[$note]+0.5)
    $bw=fmt($(if(isWhite $note){$wkW-1}else{$bkW}))
    $by=fmt($y-$bh); $bhS=fmt($bh)
    $null=$sb.AppendLine("<rect x='$bx' y='$by' width='$bw' height='$bhS' fill='$fill' opacity='$op' rx='1'/>")
}

# Glow line at keyboard boundary
$null=$sb.AppendLine("<rect x='0' y='$($kbY-5)' width='$W' height='10' fill='url(#glowLine)'/>")

# White keys
$litB=@(60,64,67); $litR=@(57,60)
for($i=0;$i-lt 128;$i++){
    if(-not (isWhite $i)){ continue }
    $kx=fmt($noteX[$i]); $kw=fmt($wkW)
    $fill='#d4d4c8'
    if($litB -contains $i -and $litR -contains $i){ $fill='#9955cc' }
    elseif($litB -contains $i){ $fill='#44aaff' }
    elseif($litR -contains $i){ $fill='#ff4444' }
    $null=$sb.AppendLine("<rect x='$kx' y='$kbY' width='$kw' height='$kbH' fill='$fill' stroke='#555' stroke-width='0.4'/>")
    $null=$sb.AppendLine("<rect x='$kx' y='$kbY' width='$kw' height='$kbH' fill='url(#wkShad)'/>")
}

# Black keys
$litBB=@(61,63,66,68,70); $litRB=@(58,61)
for($i=0;$i-lt 128;$i++){
    if(isWhite $i){ continue }
    $kx=fmt($noteX[$i]); $kw=fmt($bkW); $kbhS=fmt($bkH)
    $fill='#111111'
    if($litBB -contains $i){ $fill='#0066aa' }
    elseif($litRB -contains $i){ $fill='#880000' }
    $null=$sb.AppendLine("<rect x='$kx' y='$kbY' width='$kw' height='$kbhS' fill='$fill' rx='1'/>")
}

$null=$sb.AppendLine("</svg>")
$sb.ToString() | Set-Content "$OutDir\piano-roll.svg" -Encoding UTF8
Write-Host "piano-roll.svg done"

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# 2. DRUM GRID helper
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
function Write-DrumSvg($file, $W, $H, $cols, $rows, $cPanel, $cIdle, $cHit, $padStyle, $labels, $hitMap, $decayMap) {
    $minDim = [Math]::Min($W/$cols, $H/$rows)
    $gap    = [Math]::Max(2.0, $minDim*0.05)
    $padW   = ($W - $gap*($cols+1)) / $cols
    $padH   = ($H - $gap*($rows+1)) / $rows

    $sb=[System.Text.StringBuilder]::new()
    $null=$sb.AppendLine("<?xml version='1.0' encoding='UTF-8'?>")
    $null=$sb.AppendLine("<svg width='$W' height='$H' viewBox='0 0 $W $H' xmlns='http://www.w3.org/2000/svg'>")

    # Panel background with subtle gradient
    $null=$sb.AppendLine("<defs>")
    $null=$sb.AppendLine("  <radialGradient id='hg' cx='50%' cy='50%' r='50%'>")
    $null=$sb.AppendLine("    <stop offset='0'   stop-color='#ffffff' stop-opacity='0.18'/>")
    $null=$sb.AppendLine("    <stop offset='100%' stop-color='#ffffff' stop-opacity='0'/>")
    $null=$sb.AppendLine("  </radialGradient>")
    $null=$sb.AppendLine("</defs>")
    $null=$sb.AppendLine("<rect width='$W' height='$H' fill='$cPanel'/>")

    for($i=0; $i -lt $cols*$rows; $i++){
        $col = $i % $cols
        $row = [Math]::Floor($i / $cols)
        $cellX = $gap + $col*($padW+$gap)
        $cellY = $gap + $row*($padH+$gap)

        # Pad shape
        $dw=$padW; $dh=$padH; $ox=0; $oy=0; $rx=2
        if($padStyle -eq 1){ $dw=$padW*0.84; $dh=$padH*0.84; $ox=($padW-$dw)/2; $oy=($padH-$dh)/2; $rx=4 }
        if($padStyle -eq 2){ $d=[Math]::Min($padW,$padH)*0.78; $dw=$d; $dh=$d; $ox=($padW-$d)/2; $oy=($padH-$d)/2; $rx=[int]($d/2) }

        $px=fmt($cellX+$ox); $py=fmt($cellY+$oy); $dwS=fmt($dw); $dhS=fmt($dh)

        $t=0.0
        if($hitMap -contains $i){ $t=1.0 }
        elseif($decayMap -and $decayMap.ContainsKey($i)){ $t=$decayMap[$i] }

        $padColor = lerpHex $cIdle $cHit $t

        # Shadow border for depth on rounded/circle styles
        if($padStyle -ge 1){
            $sx=fmt($cellX+$ox-1); $sy=fmt($cellY+$oy-1); $sdw=fmt($dw+2); $sdh=fmt($dh+2)
            $null=$sb.AppendLine("<rect x='$sx' y='$sy' width='$sdw' height='$sdh' fill='rgba(0,0,0,0.5)' rx='$([int]($rx+1))'/>")
        }

        $null=$sb.AppendLine("<rect x='$px' y='$py' width='$dwS' height='$dhS' fill='$padColor' rx='$rx'/>")

        # Hit radial glow
        if($t -gt 0.05){
            $null=$sb.AppendLine("<rect x='$px' y='$py' width='$dwS' height='$dhS' fill='url(#hg)' rx='$rx'/>")
        }

        # Label
        $label = if($labels -and $i -lt $labels.Count){ $labels[$i] }else{ '' }
        if($label -ne ''){
            $lx = fmt($cellX+$ox+$dw/2)
            $ly = fmt($cellY+$oy+$dh*0.74)
            $l=hexLum($padColor)
            $textFill = if($l -gt 0.45){ 'rgba(0,0,0,0.85)' }else{ 'rgba(255,255,255,0.85)' }
            $fs = [Math]::Max(8, [Math]::Min([int]($dh*0.15), [int]($dw*0.26)))
            $null=$sb.AppendLine("<text x='$lx' y='$ly' fill='$textFill' font-family='Courier New,monospace' font-weight='bold' font-size='${fs}px' text-anchor='middle'>$label</text>")
        }
    }

    $null=$sb.AppendLine("</svg>")
    $sb.ToString() | Set-Content $file -Encoding UTF8
    Write-Host "$(Split-Path $file -Leaf) done"
}

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# 3. TR-808
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
Write-DrumSvg "$OutDir\drum-808.svg" 480 480 4 4 '#1C1208' '#2E1E08' '#FF8800' 0 `
    @('Bass2','Bass1','Rimshot','Snare','Clap','Snare2','Tom1','HH-C',
      'Tom2','HH-P','Tom3','HH-O','Tom4','Tom5','Crash1','Tom6') `
    @(1,3,6,10) `
    @{0=0.55; 5=0.30; 9=0.20; 14=0.12}

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# 4. Novation Launchpad X  (8Ã—8, rounded pads, purple)
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
Write-DrumSvg "$OutDir\drum-launchpad.svg" 480 480 8 8 '#060606' '#0E0E0E' '#6633FF' 1 `
    $null `
    @(0,3,8,11,19,24,27,35,32,43,48,51,56,59) `
    @{1=0.55; 9=0.55; 17=0.55; 25=0.55; 33=0.55; 41=0.55;
      4=0.28; 12=0.28; 20=0.28; 28=0.28; 36=0.28; 44=0.28}

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# 5. CC Lanes  560 Ã— 140
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
$W=560; $H=140
$sb=[System.Text.StringBuilder]::new()
$null=$sb.AppendLine("<?xml version='1.0' encoding='UTF-8'?>")
$null=$sb.AppendLine("<svg width='$W' height='$H' viewBox='0 0 $W $H' xmlns='http://www.w3.org/2000/svg'>")
$null=$sb.AppendLine("<defs>")
$null=$sb.AppendLine("  <linearGradient id='barGrad1' x1='0' y1='0' x2='0' y2='1'>")
$null=$sb.AppendLine("    <stop offset='0' stop-color='#44ffcc'/>")
$null=$sb.AppendLine("    <stop offset='1' stop-color='#0055aa'/>")
$null=$sb.AppendLine("  </linearGradient>")
$null=$sb.AppendLine("  <linearGradient id='barGrad0' x1='0' y1='0' x2='0' y2='1'>")
$null=$sb.AppendLine("    <stop offset='0' stop-color='#00aaff'/>")
$null=$sb.AppendLine("    <stop offset='1' stop-color='#0055aa'/>")
$null=$sb.AppendLine("  </linearGradient>")
$null=$sb.AppendLine("</defs>")
$null=$sb.AppendLine("<rect width='$W' height='$H' fill='#111111'/>")

$labels2=@('Mod','Breath','Expr','Sustain','Vol','Pan','Filter','Reso')
$vals2=@(0.74, 0.28, 0.92, 1.0, 0.62, 0.50, 0.44, 0.18)
$labelH=26.0; $barArea=$H-$labelH-6
$laneW=[double]$W/8

for($i=0; $i -lt 8; $i++){
    $x=$i*$laneW+2
    $bh=$vals2[$i]*$barArea
    $by=$H-$labelH-$bh
    $xS=fmt($x); $byS=fmt($by); $bhS=fmt($bh); $lwS=fmt($laneW-4)
    $baS=fmt($barArea); $baY=fmt($H-$labelH-$barArea)

    # Trough
    $null=$sb.AppendLine("<rect x='$xS' y='$baY' width='$lwS' height='$baS' fill='#1a1a30' rx='2'/>")

    # Bar
    $gradId = if($vals2[$i] -gt 0.8){ 'barGrad1' }else{ 'barGrad0' }
    $null=$sb.AppendLine("<rect x='$xS' y='$byS' width='$lwS' height='$bhS' fill='url(#$gradId)' rx='2'/>")

    # Label
    $lx=fmt($x+($laneW-4)/2)
    $null=$sb.AppendLine("<text x='$lx' y='$($H-$labelH+15)' fill='#8888bb' font-family='Courier New,monospace' font-size='9px' text-anchor='middle'>$($labels2[$i])</text>")

    # Value
    if($bh -gt 12){
        $vy=fmt($by-3)
        $pct=[int]($vals2[$i]*100)
        $null=$sb.AppendLine("<text x='$lx' y='$vy' fill='rgba(200,220,255,0.75)' font-family='Courier New,monospace' font-size='8px' text-anchor='middle'>$pct</text>")
    }
}
$null=$sb.AppendLine("</svg>")
$sb.ToString() | Set-Content "$OutDir\cc-lanes.svg" -Encoding UTF8
Write-Host "cc-lanes.svg done"

# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# 6. Maschine Studio  (for variety â€” dark rounded pads, orange hit)
# â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
Write-DrumSvg "$OutDir\drum-maschine.svg" 480 480 4 4 '#080808' '#141414' '#FF3300' 1 `
    @('Bass2','Bass1','Rimshot','Snare','Clap','Snare2','Tom1','HH-C',
      'Tom2','HH-P','Tom3','HH-O','Tom4','Tom5','Crash1','Tom6') `
    @(0,3,7,10) `
    @{1=0.65; 5=0.25; 9=0.15; 14=0.10; 2=0.08}

Write-Host "All mockup SVGs written to $OutDir"

