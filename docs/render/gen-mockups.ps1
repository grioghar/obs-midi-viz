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


# ─────────────────────────────────────────────────────────────────────────────
# 7. STEP-SEQUENCER helper
# ─────────────────────────────────────────────────────────────────────────────
# rowLabels : string array, one entry per instrument row
# cPanel    : '#RRGGBB' panel / canvas background
# cIdle     : '#RRGGBB' unlit step cell
# cHit      : '#RRGGBB' triggered step cell (full flash)
# flashMap  : hashtable keyed "row,col" → 0.0–1.0 flash value
# curCol    : integer 0-15, the currently-active step column
function Write-StepSeqSvg($file, $W, $H, $rowLabels, $cPanel, $cIdle, $cHit, $flashMap, $curCol) {
    $numRows = $rowLabels.Count
    $numCols = 16
    $labelW  = [double]($W * 0.15)
    $gridW   = [double]($W - $labelW)
    $colW    = $gridW / $numCols
    $rowH    = [double]$H / $numRows
    $gap     = [Math]::Max(1.5, [Math]::Min($colW, $rowH) * 0.05)

    # Derived colours
    $hlColor      = lerpHex $cPanel $cIdle 0.55          # current-column band
    $sepColor     = lerpHex $cPanel '#888888' 0.32        # 4-step group separators
    $stripSepCol  = lerpHex $cPanel '#666666' 0.38        # label/grid boundary
    $labelColor   = '#AAAAAA'

    # Font size: target fitting 3 chars in the label strip at a readable size
    $fsW      = ($labelW - 8.0) / 11.0          # 3 chars = 11 px-cols at scale 1
    $fsH      = $rowH * 0.55
    $fontSize = [Math]::Max(9, [Math]::Min([int]$fsW, [int]$fsH))

    $sb = [System.Text.StringBuilder]::new()
    $null = $sb.AppendLine("<?xml version='1.0' encoding='UTF-8'?>")
    $null = $sb.AppendLine("<svg width='$W' height='$H' viewBox='0 0 $W $H' xmlns='http://www.w3.org/2000/svg'>")

    # Panel
    $null = $sb.AppendLine("<rect width='$W' height='$H' fill='$cPanel'/>")

    # Current-column highlight band
    $hx = fmt($labelW + [double]$curCol * $colW)
    $hw = fmt($colW)
    $null = $sb.AppendLine("<rect x='$hx' y='0' width='$hw' height='$H' fill='$hlColor'/>")

    # Group separators every 4 steps (1 bar)
    for ($g = 4; $g -lt $numCols; $g += 4) {
        $sx = fmt($labelW + [double]$g * $colW - 1.0)
        $null = $sb.AppendLine("<rect x='$sx' y='0' width='2' height='$H' fill='$sepColor'/>")
    }

    # Label-strip right-edge separator
    $lsx = fmt($labelW - 1.0)
    $null = $sb.AppendLine("<rect x='$lsx' y='0' width='2' height='$H' fill='$stripSepCol'/>")

    # Rows
    for ($r = 0; $r -lt $numRows; $r++) {
        $rowY = [double]$r * $rowH

        # Row label — centred in the strip
        $lbl = $rowLabels[$r]
        $lx  = fmt($labelW * 0.5)
        $ly  = fmt($rowY + $rowH * 0.5 + $fontSize * 0.38)
        $null = $sb.AppendLine("<text x='$lx' y='$ly' fill='$labelColor' font-family='Courier New,monospace' font-weight='bold' font-size='${fontSize}px' text-anchor='middle'>$lbl</text>")

        # Step cells
        for ($c = 0; $c -lt $numCols; $c++) {
            $key   = "$r,$c"
            $flash = if ($flashMap.ContainsKey($key)) { $flashMap[$key] } else { 0.0 }

            $cellX = fmt($labelW + [double]$c * $colW + $gap)
            $cellY = fmt($rowY + $gap)
            $cellW = fmt($colW - $gap * 2.0)
            $cellH = fmt($rowH - $gap * 2.0)
            $col   = lerpHex $cIdle $cHit $flash

            $null = $sb.AppendLine("<rect x='$cellX' y='$cellY' width='$cellW' height='$cellH' fill='$col' rx='1'/>")

            # Subtle highlight overlay on hot cells
            if ($flash -gt 0.55) {
                $null = $sb.AppendLine("<rect x='$cellX' y='$cellY' width='$cellW' height='$cellH' fill='rgba(255,255,255,0.10)' rx='1'/>")
            }
        }
    }

    $null = $sb.AppendLine("</svg>")
    $sb.ToString() | Set-Content $file -Encoding UTF8
    Write-Host "$(Split-Path $file -Leaf) done"
}

# ─────────────────────────────────────────────────────────────────────────────
# 8. TR-808 step-sequencer   640 × 320
#    Classic boom-bap pattern; current step = col 8 (beat 3, 9th 16th-note)
#    BD: 0,4,8,12  SD: 4,12  CP: 8  CH: 0,2,4,6,8  OH: 7  LT: 2  MT: 6
# ─────────────────────────────────────────────────────────────────────────────
Write-StepSeqSvg "$OutDir\seq-808.svg" 640 320 `
    @('BD','SD','CP','CH','OH','LT','MT','HT') '#1C1208' '#2E1E08' '#FF8800' `
    @{
        # -- Steps 0-7 played, fading --
        '0,0'=0.20; '3,0'=0.14;           # col 0: BD + CH (most decayed)
        '3,2'=0.22; '5,2'=0.28;           # col 2: CH + LT
        '0,4'=0.42; '1,4'=0.42; '3,4'=0.35; # col 4: BD + SD + CH
        '3,6'=0.65; '6,6'=0.62;           # col 6: CH + MT
        '4,7'=0.60;                        # col 7: OH
        # -- Col 8: current step (just fired) --
        '0,8'=1.0; '2,8'=1.0; '3,8'=1.0  # BD + CP + CH fully lit
    } 8

# ─────────────────────────────────────────────────────────────────────────────
# 9. TB-303 step-sequencer (bassline, chromatic)   640 × 480
#    A-minor acid figure; current step = col 8 (G firing)
#    Pattern: A(col0) C(2) D(3) E(4) G(5) A(6) C(7) G(8) ...
# ─────────────────────────────────────────────────────────────────────────────
Write-StepSeqSvg "$OutDir\seq-tb303.svg" 640 480 `
    @('C','C#','D','D#','E','F','F#','G','G#','A','A#','B') '#061A0A' '#0D2E14' '#00FF66' `
    @{
        '9,0'=0.22;   # A at col 0 (most faded)
        '0,2'=0.40;   # C
        '2,3'=0.38;   # D
        '4,4'=0.55;   # E
        '7,5'=0.62;   # G
        '9,6'=0.72;   # A
        '0,7'=0.82;   # C (most recent before current)
        '7,8'=1.0     # G at col 8 — current, fully lit
    } 8

Write-Host "All mockup SVGs written to $OutDir"

