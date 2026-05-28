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

# ─────────────────────────────────────────────────────────────────────────────
# 10. DJ CONTROLLER helpers — Pioneer DDJ-FLX4   1280 × 480
# ─────────────────────────────────────────────────────────────────────────────

# Return a <circle> SVG element string
function svgC($cx,$cy,$r,$fill) {
    "<circle cx='$(fmt $cx)' cy='$(fmt $cy)' r='$(fmt $r)' fill='$fill'/>"
}

# Return a <rect> SVG element string
function svgR($x,$y,$w,$h,$fill,$rx=0) {
    "<rect x='$(fmt $x)' y='$(fmt $y)' width='$(fmt $w)' height='$(fmt $h)' fill='$fill' rx='$rx'/>"
}

# Return a centred <text> SVG element string
function svgT($x,$y,$text,$fill,$sz) {
    "<text x='$(fmt $x)' y='$(fmt $y)' fill='$fill' font-family='Courier New,monospace' font-size='${sz}px' font-weight='bold' text-anchor='middle'>$text</text>"
}

# Return array of SVG strings for a rotary knob with 27-dot Pioneer LED arc
#   value   : 0.0–1.0
#   colorOn : hex colour of lit dots
#   cDet    : $true = centre-detent (EQ style, lights from 0.5 outward)
function djKnob($cx,$cy,$r,$value,$colorOn,$cDet) {
    $PI = [Math]::PI
    $out = [System.Collections.Generic.List[string]]::new()
    $out.Add((svgC $cx $cy $r '#2E2E2E'))
    $out.Add((svgC $cx $cy ($r * 0.65) '#484848'))
    $arcR = $r + 6.0
    for ($i = 0; $i -lt 27; $i++) {
        $deg = 225.0 + [double]$i * (270.0 / 26.0)
        $rad = $deg * $PI / 180.0
        $dx  = $cx + $arcR * [Math]::Sin($rad)
        $dy  = $cy - $arcR * [Math]::Cos($rad)
        $frac = [double]$i / 26.0
        if ($cDet) {
            if ($value -ge 0.5) { $lit = ($frac -ge 0.48) -and ($frac -le ($value + 0.02)) }
            else                { $lit = ($frac -ge ($value - 0.02)) -and ($frac -le 0.52) }
        } else { $lit = $frac -le ($value + 0.02) }
        $dc = if ($lit) { $colorOn } else { '#2A2A2A' }
        $out.Add((svgC $dx $dy 2.2 $dc))
    }
    $pDeg = 225.0 + $value * 270.0
    $pRad = $pDeg * $PI / 180.0
    $out.Add((svgC ($cx + ($r * 0.42) * [Math]::Sin($pRad)) ($cy - ($r * 0.42) * [Math]::Cos($pRad)) 2.5 '#DDDDDD'))
    return $out
}

# Return array of SVG strings for a jog wheel
function djJog($cx,$cy,$r,$angleDeg,$playing) {
    $PI = [Math]::PI
    $out = [System.Collections.Generic.List[string]]::new()
    $out.Add((svgC $cx $cy $r '#606060'))                           # outer rim
    $out.Add((svgC $cx $cy ($r - 7)  '#141414'))                   # groove
    $out.Add((svgC $cx $cy ($r - 11) '#282828'))                   # platter disc
    $out.Add((svgC $cx $cy ($r * 0.25) '#5A5A5A'))                 # hub ring
    $out.Add((svgC $cx $cy ($r * 0.16) '#383838'))                 # hub centre
    # Rotating platter dot
    $dotR  = $r - 24.0
    $dRad  = $angleDeg * $PI / 180.0
    $dox   = $cx + $dotR * [Math]::Sin($dRad)
    $doy   = $cy - $dotR * [Math]::Cos($dRad)
    $out.Add((svgC $dox $doy 4.5 '#999999'))
    # Playing indicator — continuous green stroke ring
    if ($playing) {
        $rStr = fmt($r - 4.5)
        $out.Add("<circle cx='$(fmt $cx)' cy='$(fmt $cy)' r='$rStr' fill='none' stroke='#00CC44' stroke-width='3' stroke-opacity='0.85'/>")
    }
    return $out
}

# Return array of SVG strings for a vertical fader (value 0=bottom, 1=top)
function djVFader($cx,$y1,$y2,$value,$colorFill) {
    $trackH = $y2 - $y1
    $hy = $y2 - $value * $trackH
    $out = [System.Collections.Generic.List[string]]::new()
    $out.Add((svgR ($cx - 4)  $y1 8 $trackH '#333333' 3))          # track
    if (($y2 - $hy) -gt 0) {
        $out.Add((svgR ($cx - 2.5) $hy 5 ($y2 - $hy) $colorFill 2)) # fill
    }
    $out.Add((svgR ($cx - 11) ($hy - 6)   22 12 '#777777' 2))      # handle cap
    $out.Add((svgR ($cx -  9) ($hy - 1.5) 18  3 '#CCCCCC' 1))      # cap groove
    return $out
}

# Return array of SVG strings for a horizontal fader (value 0=left, 1=right)
function djHFader($x1,$x2,$y,$value) {
    $trackW = $x2 - $x1
    $hx = $x1 + $value * $trackW
    $out = [System.Collections.Generic.List[string]]::new()
    $out.Add((svgR $x1 ($y - 4) $trackW 8 '#333333' 3))
    if (($hx - $x1) -gt 0) {
        $out.Add((svgR $x1 ($y - 3) ($hx - $x1) 6 '#FF8800' 2))
    }
    $out.Add((svgR ($hx - 7)   ($y - 13) 14 26 '#777777' 2))
    $out.Add((svgR ($hx - 1.5) ($y - 11)  3 22 '#CCCCCC' 1))
    return $out
}

# Return array of SVG strings for a labelled transport button
function djButton($x,$y,$w,$h,$active,$colorAct,$label) {
    $bg  = if ($active) { $colorAct } else { '#2A2A2A' }
    $brd = if ($active) { $colorAct } else { '#4A4A4A' }
    $tc  = if ($active) { '#FFFFFF'  } else { '#666666' }
    $out = [System.Collections.Generic.List[string]]::new()
    $out.Add((svgR $x $y $w $h $bg 3))
    $out.Add("<rect x='$(fmt $x)' y='$(fmt $y)' width='$(fmt $w)' height='$(fmt $h)' fill='none' stroke='$brd' stroke-width='1' rx='3'/>")
    if ($label -ne '') { $out.Add((svgT ($x + $w * 0.5) ($y + $h * 0.5 + 4) $label $tc 9)) }
    return $out
}

# Append all lines from a list/array to a StringBuilder
function djAdd($sb, $lines) { foreach ($l in $lines) { $null = $sb.AppendLine($l) } }

# ─────────────────────────────────────────────────────────────────────────────
# 11. Write DJ controller mockup SVG — Pioneer DDJ-FLX4 (corrected layout)
#
# MIDI-accurate layout verified against DDJ-FLX4_MIDI_message_List_E1.pdf:
#   Deck 1 transport/EQ: MIDI ch 1 (0x90/0xB0)
#   Deck 2 transport/EQ: MIDI ch 2 (0x91/0xB1)
#   Crossfader/Filter:   MIDI ch 7 (0xB6)
#   Pads Deck 1 press:   MIDI ch 8 (0x97) — notes 0x00-0x07 per pad
#   Pads Deck 2 press:   MIDI ch 10 (0x99)
#
#  Layout changes from previous version:
#   • Jog r=110 (was 130) to fit 8-pad grid + loop/mode buttons below
#   • 3 transport buttons (PLAY/CUE/SYNC) instead of 4
#   • NEW: LOOP-IN | RELOOP | LOOP-OUT row above transport
#   • NEW: Pad-mode selector (HC | LOOP | JUMP | SAMP)
#   • 8 performance pads in 2×4 grid (was 4 pads)
# ─────────────────────────────────────────────────────────────────────────────

# Helper: draw 8 pads (2 rows × 4) centred at jogCx, starting at padY1
function djPads8($sb, $jogCx, $padY1, $hcColors8, $hcFlash8) {
    $pW=46; $pH=26; $pSp=3
    $pRW=4*$pW+3*$pSp
    $px0=[double]$jogCx - $pRW/2.0
    for ($p=0;$p-lt 8;$p++) {
        $row=$p/4; $col=$p%4
        $hc=$hcColors8[$p%4]; $fl=$hcFlash8[$p]; $bright=0.22+$fl*0.78
        $hr=[Convert]::ToInt32($hc.Substring(1,2),16)
        $hg=[Convert]::ToInt32($hc.Substring(3,2),16)
        $hb=[Convert]::ToInt32($hc.Substring(5,2),16)
        $pc='#{0:X2}{1:X2}{2:X2}' -f ([int]($hr*$bright)),([int]($hg*$bright)),([int]($hb*$bright))
        $px=[double]$px0+[double]$col*($pW+$pSp)
        $py=if ($row -eq 0) { $padY1 } else { $padY1+$pH+3 }
        $null = $sb.AppendLine((svgR $px $py $pW $pH $pc 3))
        $null = $sb.AppendLine("<rect x='$(fmt $px)' y='$(fmt $py)' width='$(fmt $pW)' height='$(fmt $pH)' fill='none' stroke='#555555' stroke-width='1' rx='3'/>")
        $null = $sb.AppendLine((svgT ([double]$px+$pW*0.5) ([double]$py+$pH*0.5+4) ($p+1) '#FFFFFF' 8))
    }
}

function Write-DjSvg($file) {
    $W = 1280; $H = 480
    $sb = [System.Text.StringBuilder]::new()
    $null = $sb.AppendLine("<?xml version='1.0' encoding='UTF-8'?>")
    $null = $sb.AppendLine("<svg width='$W' height='$H' viewBox='0 0 $W $H' xmlns='http://www.w3.org/2000/svg'>")
    $null = $sb.AppendLine((svgR 0 0 $W $H '#111111'))

    # Shared pad data
    $hcColors=@('#FF2244','#2266FF','#22AA22','#FF8800')

    # ── DECK 0 ───────────────────────────────────────────────────────────────
    $null = $sb.AppendLine((svgR 0 0 558 $H '#1E1E1E'))
    $null = $sb.AppendLine((svgR 556 0 2 $H '#3A3A3A'))

    # Jog wheel — cx=230, cy=190, r=110, platter dot at 20°, PLAYING
    $jog0cx=230; $jog0cy=190; $jog0r=110
    djAdd $sb (djJog $jog0cx $jog0cy $jog0r 20 $true)

    # Knob column (deck 0: right side, x=425)
    # CC: EQ HI=0x07, MID=0x0B, LOW=0x0F, TRIM=0x04, FILTER=0x17 (ch7)
    $null = $sb.AppendLine((svgT 425  50 'HI'   '#AAAAAA' 8))
    $null = $sb.AppendLine((svgT 425 124 'MID'  '#AAAAAA' 8))
    $null = $sb.AppendLine((svgT 425 198 'LO'   '#AAAAAA' 8))
    $null = $sb.AppendLine((svgT 425 264 'TRIM' '#AAAAAA' 8))
    $null = $sb.AppendLine((svgT 425 318 'FILT' '#AAAAAA' 8))
    djAdd $sb (djKnob 425  76 22 0.72 '#FF8800' $true)    # EQ Hi boosted (CC 0x07)
    djAdd $sb (djKnob 425 150 22 0.50 '#FF8800' $true)    # EQ Mid flat   (CC 0x0B)
    djAdd $sb (djKnob 425 224 22 0.30 '#FF8800' $true)    # EQ Low cut    (CC 0x0F)
    djAdd $sb (djKnob 425 288 18 0.68 '#FFDD00' $false)   # Trim          (CC 0x04)
    djAdd $sb (djKnob 425 342 18 0.55 '#00AAFF' $false)   # Filter        (CC 0x17 ch7)

    # Volume fader (right of knob column, x=510, CC 0x13)
    djAdd $sb (djVFader 510 48 368 0.78 '#FF8800')
    $null = $sb.AppendLine((svgT 510 376 'VOL' '#AAAAAA' 8))

    # Tempo fader (far left, x=50, CC 0x00)
    djAdd $sb (djVFader 50 48 368 0.52 '#888888')
    $null = $sb.AppendLine((svgT 50 376 'TEMPO' '#AAAAAA' 8))

    # Loop controls row: IN | RELOOP | OUT  (notes 0x10, 0x4D, 0x11 on ch1)
    $lbRW=3*58+2*4; $lbx0=[double]$jog0cx-$lbRW/2.0; $lbY=[double]$jog0cy+$jog0r+8; $lbH=20
    djAdd $sb (djButton $lbx0           $lbY 58 $lbH $false '#00CCFF' 'LOOP-IN')
    djAdd $sb (djButton ($lbx0+62)      $lbY 58 $lbH $false '#00CC44' 'RELOOP')
    djAdd $sb (djButton ($lbx0+124)     $lbY 58 $lbH $false '#00CCFF' 'LOOP-OUT')

    # Transport row: PLAY | CUE | SYNC  (notes 0x0B, 0x0C, 0x58 on ch1)
    $tbY=$lbY+$lbH+4; $tbH=26
    $tbRW=70+57+57+2*4; $tbx0=[double]$jog0cx-$tbRW/2.0
    djAdd $sb (djButton $tbx0            $tbY 70 $tbH $true  '#00CC44' 'PLAY')
    djAdd $sb (djButton ($tbx0+74)       $tbY 57 $tbH $false '#0088FF' 'CUE')
    djAdd $sb (djButton ($tbx0+135)      $tbY 57 $tbH $true  '#FF8800' 'SYNC')

    # Pad mode selector: HC | LOOP | JUMP | SAMP
    $modeColors=@('#FF4444','#00CCFF','#FFAA00','#AA44FF'); $modeLabels=@('HC','LOOP','JUMP','SAMP')
    $mbY=$tbY+$tbH+4; $mbH=14; $mbW=46; $mbSp=3
    $mbRW=4*$mbW+3*$mbSp; $mbx0=[double]$jog0cx-$mbRW/2.0
    for ($m=0;$m-lt 4;$m++) {
        $mx=[double]$mbx0+[double]$m*($mbW+$mbSp)
        $mc=$modeColors[$m]; $active=($m -eq 0)  # HC active
        $bg=if ($active) { $mc } else { '#222222' }
        $tc=if ($active) { '#FFFFFF' } else { '#666666' }
        $null = $sb.AppendLine((svgR $mx $mbY $mbW $mbH $bg 2))
        $null = $sb.AppendLine("<rect x='$(fmt $mx)' y='$(fmt $mbY)' width='$(fmt $mbW)' height='$(fmt $mbH)' fill='none' stroke='$mc' stroke-width='1' rx='2'/>")
        $null = $sb.AppendLine((svgT ([double]$mx+$mbW*0.5) ([double]$mbY+$mbH*0.5+3.5) $modeLabels[$m] $tc 8))
    }

    # 8 performance pads (2×4) — HC1 flashing, rest dim (ch8, notes 0x00-0x07)
    $padY=[double]$mbY+$mbH+3
    $hcFlash0=@(0.85, 0.22, 0.22, 0.22, 0.22, 0.22, 0.22, 0.22)
    djPads8 $sb $jog0cx $padY $hcColors $hcFlash0

    # ── MIXER ────────────────────────────────────────────────────────────────
    $null = $sb.AppendLine((svgR 560 0 160 $H '#191919'))
    $null = $sb.AppendLine((svgR 560 0   1 $H '#3A3A3A'))
    $null = $sb.AppendLine((svgR 719 0   1 $H '#3A3A3A'))
    $null = $sb.AppendLine((svgT 640 22 'DDJ-FLX4' '#FF8800' 11))
    # Crossfader: CC 0x1F on MIDI ch7 (status 0xB6)
    djAdd $sb (djHFader 578 702 420 0.38)
    $null = $sb.AppendLine((svgT 640 440 'XFADER' '#AAAAAA' 8))
    $null = $sb.AppendLine((svgT 640 453 'CC 0x1F  ch7' '#444444' 7))

    # ── DECK 1 ───────────────────────────────────────────────────────────────
    $null = $sb.AppendLine((svgR 720 0 560 $H '#1E1E1E'))
    $null = $sb.AppendLine((svgR 720 0   2 $H '#3A3A3A'))

    # Jog wheel — cx=1050, cy=190, r=110, platter dot at 240°, NOT playing
    $jog1cx=1050; $jog1cy=190; $jog1r=110
    djAdd $sb (djJog $jog1cx $jog1cy $jog1r 240 $false)

    # Knob column (deck 1 mirrored: knobX=856)
    $null = $sb.AppendLine((svgT 856  50 'HI'   '#AAAAAA' 8))
    $null = $sb.AppendLine((svgT 856 124 'MID'  '#AAAAAA' 8))
    $null = $sb.AppendLine((svgT 856 198 'LO'   '#AAAAAA' 8))
    $null = $sb.AppendLine((svgT 856 264 'TRIM' '#AAAAAA' 8))
    $null = $sb.AppendLine((svgT 856 318 'FILT' '#AAAAAA' 8))
    djAdd $sb (djKnob 856  76 22 0.50 '#FF8800' $true)
    djAdd $sb (djKnob 856 150 22 0.50 '#FF8800' $true)
    djAdd $sb (djKnob 856 224 22 0.50 '#FF8800' $true)
    djAdd $sb (djKnob 856 288 18 0.55 '#FFDD00' $false)
    djAdd $sb (djKnob 856 342 18 0.44 '#00AAFF' $false)

    # Volume fader (mirrored: left of knob column, x=772)
    djAdd $sb (djVFader 772 48 368 0.62 '#FF8800')
    $null = $sb.AppendLine((svgT 772 376 'VOL' '#AAAAAA' 8))

    # Tempo fader (mirrored: far right, x=1230)
    djAdd $sb (djVFader 1230 48 368 0.50 '#888888')
    $null = $sb.AppendLine((svgT 1230 376 'TEMPO' '#AAAAAA' 8))

    # Loop controls row (deck 1, mirrored — notes on ch2)
    $lb1RW=3*58+2*4; $lb1x0=[double]$jog1cx-$lb1RW/2.0; $lb1Y=[double]$jog1cy+$jog1r+8
    djAdd $sb (djButton $lb1x0           $lb1Y 58 $lbH $false '#00CCFF' 'LOOP-IN')
    djAdd $sb (djButton ($lb1x0+62)      $lb1Y 58 $lbH $false '#00CC44' 'RELOOP')
    djAdd $sb (djButton ($lb1x0+124)     $lb1Y 58 $lbH $false '#00CCFF' 'LOOP-OUT')

    # Transport row (deck 1)
    $tb1Y=$lb1Y+$lbH+4; $tb1RW=$tbRW; $tb1x0=[double]$jog1cx-$tb1RW/2.0
    djAdd $sb (djButton $tb1x0           $tb1Y 70 $tbH $false '#00CC44' 'PLAY')
    djAdd $sb (djButton ($tb1x0+74)      $tb1Y 57 $tbH $false '#0088FF' 'CUE')
    djAdd $sb (djButton ($tb1x0+135)     $tb1Y 57 $tbH $false '#FF8800' 'SYNC')

    # Pad mode (deck 1 — no mode active)
    $mb1Y=$tb1Y+$tbH+4; $mb1x0=[double]$jog1cx-$mbRW/2.0
    for ($m=0;$m-lt 4;$m++) {
        $mx=[double]$mb1x0+[double]$m*($mbW+$mbSp); $mc=$modeColors[$m]
        $null = $sb.AppendLine((svgR $mx $mb1Y $mbW $mbH '#222222' 2))
        $null = $sb.AppendLine("<rect x='$(fmt $mx)' y='$(fmt $mb1Y)' width='$(fmt $mbW)' height='$(fmt $mbH)' fill='none' stroke='$mc' stroke-width='1' rx='2'/>")
        $null = $sb.AppendLine((svgT ([double]$mx+$mbW*0.5) ([double]$mb1Y+$mbH*0.5+3.5) $modeLabels[$m] '#666666' 8))
    }

    # 8 pads deck 1 (all dim, ch10, notes 0x00-0x07)
    $pad1Y=[double]$mb1Y+$mbH+3
    $hcFlash1=@(0.22,0.22,0.22,0.22,0.22,0.22,0.22,0.22)
    djPads8 $sb $jog1cx $pad1Y $hcColors $hcFlash1

    $null = $sb.AppendLine("</svg>")
    $sb.ToString() | Set-Content $file -Encoding UTF8
    Write-Host "$(Split-Path $file -Leaf) done"
}

Write-DjSvg "$OutDir\dj-flx4.svg"

# ─────────────────────────────────────────────────────────────────────────────
# 12. SYNTH PATCH DISPLAY helpers — DeepMind 12   720 × 400
# ─────────────────────────────────────────────────────────────────────────────

# Synth knob: 27-dot arc (same geometry as DJ knob, no centre-detent)
function synthKnob($cx,$cy,$r,$value,$colorOn,$label) {
    $PI  = [Math]::PI
    $out = [System.Collections.Generic.List[string]]::new()
    # Body
    $out.Add("<circle cx='$(fmt $cx)' cy='$(fmt $cy)' r='$(fmt $r)' fill='#252525'/>")
    $out.Add("<circle cx='$(fmt $cx)' cy='$(fmt $cy)' r='$(fmt ($r*0.6))' fill='#363636'/>")
    $arcR = $r + 5.5
    for ($i = 0; $i -lt 27; $i++) {
        $deg = 225.0 + [double]$i * (270.0 / 26.0)
        $rad = $deg * $PI / 180.0
        $dx  = $cx + $arcR * [Math]::Sin($rad)
        $dy  = $cy - $arcR * [Math]::Cos($rad)
        $lit = ([double]$i / 26.0) -le ($value + 0.02)
        $dc  = if ($lit) { $colorOn } else { '#1A1A1A' }
        $out.Add("<circle cx='$(fmt $dx)' cy='$(fmt $dy)' r='1.5' fill='$dc'/>")
    }
    # Indicator dot
    $ideg = 225.0 + $value * 270.0
    $irad = $ideg * $PI / 180.0
    $ix   = $cx + $r * 0.48 * [Math]::Sin($irad)
    $iy   = $cy - $r * 0.48 * [Math]::Cos($irad)
    $out.Add("<circle cx='$(fmt $ix)' cy='$(fmt $iy)' r='2' fill='#EEEEEE'/>")
    # Label
    if ($label -ne '') {
        $out.Add("<text x='$(fmt $cx)' y='$(fmt ($cy+$r+12))' fill='#666666' font-family='Courier New,monospace' font-size='8px' text-anchor='middle'>$label</text>")
    }
    return $out
}

# ADSR envelope visualizer — returns a SVG <polygon> element
function synthEnv($x,$y,$w,$h,$a,$d,$sv,$r,$col) {
    $sHold = 0.28
    $total = $a + $d + $sHold + $r
    if ($total -lt 0.01) { $total = 1.0 }
    $xA    = $x
    $xD    = $x + ($a/$total)*$w
    $xS    = $xD + ($d/$total)*$w
    $xR    = $xS + ($sHold/$total)*$w
    $xEnd  = $x + $w - 1
    $yBot  = $y + $h - 2
    $yTop  = $y + 2
    $ySus  = $yBot - $sv*($h - 4)
    $pts   = "$(fmt $xA),$(fmt $yBot) $(fmt $xD),$(fmt $yTop) $(fmt $xS),$(fmt $ySus) $(fmt $xR),$(fmt $ySus) $(fmt $xEnd),$(fmt $yBot)"
    $alpha = '55'
    $fillHex = $col.TrimStart('#')
    $fill = "#${alpha}${fillHex}"
    $out  = [System.Collections.Generic.List[string]]::new()
    $out.Add("<rect x='$(fmt $x)' y='$(fmt $y)' width='$(fmt $w)' height='$(fmt $h)' fill='#0A0A0A'/>")
    $out.Add("<polygon points='$pts' fill='$fill'/>")
    $out.Add("<polyline points='$pts' fill='none' stroke='#$($col.TrimStart('#'))' stroke-width='1.5'/>")
    return $out
}

# Horizontal level bar
function synthHBar($x,$y,$w,$h,$val,$col) {
    $bw   = $val * ($w - 4)
    $out  = [System.Collections.Generic.List[string]]::new()
    $out.Add("<rect x='$(fmt $x)' y='$(fmt $y)' width='$(fmt $w)' height='$(fmt $h)' fill='#0A0A0A' rx='1'/>")
    if ($bw -gt 1) { $out.Add("<rect x='$(fmt ($x+2))' y='$(fmt ($y+2))' width='$(fmt $bw)' height='$(fmt ($h-4))' fill='$col' rx='1'/>") }
    return $out
}

# Section background with coloured title strip
function synthSection($x,$y,$w,$h,$title,$col) {
    $out = [System.Collections.Generic.List[string]]::new()
    $out.Add("<rect x='$(fmt $x)' y='$(fmt $y)' width='$(fmt $w)' height='$(fmt $h)' fill='#1C1C1C'/>")
    $out.Add("<rect x='$(fmt $x)' y='$(fmt $y)' width='$(fmt $w)' height='18' fill='${col}38'/>")
    $out.Add("<rect x='$(fmt $x)' y='$(fmt $y)' width='$(fmt $w)' height='1.5' fill='$col'/>")
    if ($title -ne '') {
        $out.Add("<text x='$(fmt ($x+$w*0.5))' y='$(fmt ($y+12))' fill='$col' font-family='Courier New,monospace' font-size='8px' font-weight='bold' text-anchor='middle'>$title</text>")
    }
    return $out
}

function synthAdd($sb, $lines) { foreach ($l in $lines) { $null = $sb.AppendLine($l) } }

# ─────────────────────────────────────────────────────────────────────────────
# 13. Write DeepMind 12 synth panel SVG
#     Shows a realistic patch: pads/strings character with reverb & chorus
# ─────────────────────────────────────────────────────────────────────────────
function Write-SynthSvg($file) {
    $W = 720; $H = 400
    $sb = [System.Text.StringBuilder]::new()
    $null = $sb.AppendLine("<?xml version='1.0' encoding='UTF-8'?>")
    $null = $sb.AppendLine("<svg width='$W' height='$H' viewBox='0 0 $W $H' xmlns='http://www.w3.org/2000/svg'>")

    # Canvas background
    $null = $sb.AppendLine("<rect width='$W' height='$H' fill='#111111'/>")

    # ── Header (y=0, h=36) ──────────────────────────────────────────────────
    $null = $sb.AppendLine("<rect x='0' y='0' width='$W' height='36' fill='#071820'/>")
    $null = $sb.AppendLine("<text x='10' y='24' fill='#FF8800' font-family='Courier New,monospace' font-size='14px' font-weight='bold'>DEEPMIND 12</text>")
    $null = $sb.AppendLine("<text x='360' y='24' fill='#CCCCCC' font-family='Courier New,monospace' font-size='14px' font-weight='bold' text-anchor='middle'>AURORA PAD</text>")
    $null = $sb.AppendLine("<text x='$(fmt ($W-8))' y='24' fill='#00CC44' font-family='Courier New,monospace' font-size='9px' text-anchor='end'>PATCH OK</text>")

    # Patch parameters (pads/strings character)
    $vco1  = @(0.50, 0.50, 0.65, 0.75)    # OCT TUN PW MIX
    $vco2  = @(0.50, 0.53, 0.50, 0.25)    # slight detune
    $filt  = @(0.58, 0.28, 0.62, 0.50)    # CUT RES ENV KEY
    $env1  = @(0.18, 0.45, 0.55, 0.48)    # Filter ADSR
    $env2  = @(0.12, 0.00, 1.00, 0.35)    # Amp ADSR (slow attack, full sustain)
    $lfo1  = @(0.22, 0.18, 0.00)          # slow sine mod
    $lfo2  = @(0.38, 0.12, 0.17)
    $fx    = @(0.50, 0.65, 0.32)          # CHO REV DLY

    # Layout constants
    $R1Y=38; $R1H=108; $R2Y=148; $R2H=108; $R3Y=258; $R3H=80; $R4Y=340; $R4H=60
    $KY1 = $R1Y + 18 + ($R1H-18)/2    # knob centre Y for row 1

    # ── OSC row ──────────────────────────────────────────────────────────────
    synthAdd $sb (synthSection  0  $R1Y 180 $R1H "VCO1" "#66AAFF")
    synthAdd $sb (synthKnob  30 $KY1 18 $vco1[0] "#66AAFF" "OCT")
    synthAdd $sb (synthKnob  75 $KY1 18 $vco1[1] "#66AAFF" "TUN")
    synthAdd $sb (synthKnob 120 $KY1 18 $vco1[2] "#66AAFF" "PW")
    synthAdd $sb (synthKnob 155 $KY1 16 $vco1[3] "#4488CC" "MIX")

    synthAdd $sb (synthSection 182 $R1Y 178 $R1H "VCO2" "#66AAFF")
    synthAdd $sb (synthKnob 212 $KY1 18 $vco2[0] "#66AAFF" "OCT")
    synthAdd $sb (synthKnob 257 $KY1 18 $vco2[1] "#66AAFF" "TUN")
    synthAdd $sb (synthKnob 302 $KY1 18 $vco2[2] "#66AAFF" "PW")
    synthAdd $sb (synthKnob 337 $KY1 16 $vco2[3] "#4488CC" "MIX")

    synthAdd $sb (synthSection 362 $R1Y 178 $R1H "FILTER" "#0088FF")
    synthAdd $sb (synthKnob 392 $KY1 18 $filt[0] "#0088FF" "CUT")
    synthAdd $sb (synthKnob 437 $KY1 18 $filt[1] "#0088FF" "RES")
    synthAdd $sb (synthKnob 482 $KY1 18 $filt[2] "#44AAFF" "ENV")
    synthAdd $sb (synthKnob 525 $KY1 16 $filt[3] "#2266AA" "KEY")

    synthAdd $sb (synthSection 542 $R1Y 178 $R1H "AMP" "#AADD55")
    synthAdd $sb (synthKnob 599 $KY1 22 0.75 "#AADD55" "VOL")
    synthAdd $sb (synthKnob 670 $KY1 22 0.50 "#88AA44" "PAN")

    # ── ENV row ──────────────────────────────────────────────────────────────
    synthAdd $sb (synthSection  0 $R2Y 360 $R2H "FILTER ENV" "#0088FF")
    synthAdd $sb (synthEnv 4 ($R2Y+20) 352 ($R2H-24) $env1[0] $env1[1] $env1[2] $env1[3] "0088FF")

    synthAdd $sb (synthSection 362 $R2Y 358 $R2H "AMP ENV" "#00CC44")
    synthAdd $sb (synthEnv 366 ($R2Y+20) 350 ($R2H-24) $env2[0] $env2[1] $env2[2] $env2[3] "00CC44")

    # ── LFO + FX row ─────────────────────────────────────────────────────────
    $KY3 = $R3Y + 18 + ($R3H-18)/2
    synthAdd $sb (synthSection   0 $R3Y 180 $R3H "LFO1" "#AA44FF")
    synthAdd $sb (synthKnob  35 $KY3 16 $lfo1[0] "#AA44FF" "RT")
    synthAdd $sb (synthKnob  90 $KY3 16 $lfo1[1] "#AA44FF" "DP")
    synthAdd $sb (synthKnob 145 $KY3 16 $lfo1[2] "#7733CC" "WV")

    synthAdd $sb (synthSection 182 $R3Y 178 $R3H "LFO2" "#AA44FF")
    synthAdd $sb (synthKnob 217 $KY3 16 $lfo2[0] "#AA44FF" "RT")
    synthAdd $sb (synthKnob 272 $KY3 16 $lfo2[1] "#AA44FF" "DP")
    synthAdd $sb (synthKnob 327 $KY3 16 $lfo2[2] "#7733CC" "WV")

    synthAdd $sb (synthSection 362 $R3Y 358 $R3H "FX" "#FF4466")
    $by0 = $R3Y + 24
    synthAdd $sb (synthHBar 422 $by0        280 14 $fx[0] "#FF4466")
    $null=$sb.AppendLine("<text x='$(fmt 417)' y='$(fmt ($by0+10))' fill='#888888' font-family='Courier New,monospace' font-size='8px' text-anchor='end'>CHO</text>")
    synthAdd $sb (synthHBar 422 ($by0+18)   280 14 $fx[1] "#FF8833")
    $null=$sb.AppendLine("<text x='$(fmt 417)' y='$(fmt ($by0+28))' fill='#888888' font-family='Courier New,monospace' font-size='8px' text-anchor='end'>REV</text>")
    synthAdd $sb (synthHBar 422 ($by0+36)   280 14 $fx[2] "#FFDD22")
    $null=$sb.AppendLine("<text x='$(fmt 417)' y='$(fmt ($by0+46))' fill='#888888' font-family='Courier New,monospace' font-size='8px' text-anchor='end'>DLY</text>")

    # ── ARP + INFO row ────────────────────────────────────────────────────────
    synthAdd $sb (synthSection  0  $R4Y 360 $R4H "ARP" "#FF8800")
    $null=$sb.AppendLine("<circle cx='22' cy='$(fmt ($R4Y+40))' r='7' fill='#331100'/>")
    $null=$sb.AppendLine("<text x='38' y='$(fmt ($R4Y+44))' fill='#554444' font-family='Courier New,monospace' font-size='9px'>OFF</text>")

    synthAdd $sb (synthSection 362 $R4Y 358 $R4H "INFO" "#444444")
    $null=$sb.AppendLine("<text x='370' y='$(fmt ($R4Y+30))' fill='#666666' font-family='Courier New,monospace' font-size='8px'>CH 1  PORT 0</text>")

    $null = $sb.AppendLine("</svg>")
    $sb.ToString() | Set-Content $file -Encoding UTF8
    Write-Host "$(Split-Path $file -Leaf) done"
}

Write-SynthSvg "$OutDir\synth-dm12.svg"

# ─────────────────────────────────────────────────────────────────────────────
# 14. DAW SESSION VIEW helpers — Ableton Live Session View   720 × 400
# ─────────────────────────────────────────────────────────────────────────────

function dawClip($x,$y,$w,$h,$name,$playing,$hasClip,$col) {
    $out = [System.Collections.Generic.List[string]]::new()
    if (-not $hasClip) {
        # Empty slot — dim trough
        $out.Add("<rect x='$(fmt $x)' y='$(fmt $y)' width='$(fmt $w)' height='$(fmt $h)' fill='#0D0D0D' rx='2'/>")
        $out.Add("<rect x='$(fmt $x)' y='$(fmt $y)' width='$(fmt $w)' height='$(fmt $h)' fill='none' stroke='#1E1E1E' stroke-width='1' rx='2'/>")
        return $out
    }
    $bgCol  = if ($playing) { '#1A3A1A' } else { '#1C1C2A' }
    $brdCol = if ($playing) { '#00CC44' } else { $col }
    $txtCol = if ($playing) { '#00FF55' } else { '#AAAAAA' }
    $out.Add("<rect x='$(fmt $x)' y='$(fmt $y)' width='$(fmt $w)' height='$(fmt $h)' fill='$bgCol' rx='2'/>")
    $out.Add("<rect x='$(fmt $x)' y='$(fmt $y)' width='$(fmt $w)' height='$(fmt $h)' fill='none' stroke='$brdCol' stroke-width='1' rx='2'/>")
    if ($playing) {
        # Left play stripe
        $out.Add("<rect x='$(fmt $x)' y='$(fmt $y)' width='3' height='$(fmt $h)' fill='#00CC44' rx='1'/>")
        # Play triangle
        $tx = $x + 7; $ty = $y + $h/2
        $out.Add("<polygon points='$(fmt $tx),$(fmt ($ty-4)) $(fmt ($tx+7)),$(fmt $ty) $(fmt $tx),$(fmt ($ty+4))' fill='#00FF55'/>")
    }
    # Clip name (truncated)
    $maxW   = $w - 18
    $short  = if ($name.Length -gt 9) { $name.Substring(0,9) } else { $name }
    $txOffset = if ($playing) { $x + 18 } else { $x + 6 }
    $out.Add("<text x='$(fmt $txOffset)' y='$(fmt ($y+$h*0.5+4))' fill='$txtCol' font-family='Courier New,monospace' font-size='8px'>$short</text>")
    return $out
}

function dawMeter($x,$y,$w,$h,$level,$peak,$col) {
    $out  = [System.Collections.Generic.List[string]]::new()
    $out.Add("<rect x='$(fmt $x)' y='$(fmt $y)' width='$(fmt $w)' height='$(fmt $h)' fill='#0A0A0A' rx='1'/>")
    $fillH = $level * ($h - 2)
    if ($fillH -gt 1) {
        $fy = $y + $h - 1 - $fillH
        # Color-coded fill (green/orange/red)
        $mc  = if ($level -gt 0.88) { '#FF2222' } elseif ($level -gt 0.70) { '#FFAA00' } else { $col }
        $out.Add("<rect x='$(fmt ($x+1))' y='$(fmt $fy)' width='$(fmt ($w-2))' height='$(fmt $fillH)' fill='$mc' rx='1'/>")
    }
    # Peak hold hairline
    if ($peak -gt 0.01) {
        $py = $y + $h - 1 - $peak*($h-2) - 1
        $out.Add("<rect x='$(fmt ($x+1))' y='$(fmt $py)' width='$(fmt ($w-2))' height='1.5' fill='#FFFFFF88'/>")
    }
    return $out
}

function dawAdd($sb, $lines) { foreach ($l in $lines) { $null = $sb.AppendLine($l) } }

# ─────────────────────────────────────────────────────────────────────────────
# 15. Write Ableton Session View SVG
# ─────────────────────────────────────────────────────────────────────────────
function Write-DawSvg($file) {
    $W = 720; $H = 400
    $sb = [System.Text.StringBuilder]::new()
    $null = $sb.AppendLine("<?xml version='1.0' encoding='UTF-8'?>")
    $null = $sb.AppendLine("<svg width='$W' height='$H' viewBox='0 0 $W $H' xmlns='http://www.w3.org/2000/svg'>")

    # Canvas background
    $null = $sb.AppendLine("<rect width='$W' height='$H' fill='#111111'/>")

    # Layout constants
    $HEADER_H  = 36
    $TRKNAME_H = 22
    $SCENE_W   = 72
    $METER_H   = 42
    $CLIP_AREA_Y = $HEADER_H + $TRKNAME_H
    $CLIP_AREA_H = $H - $CLIP_AREA_Y - $METER_H
    $NUM_TRACKS  = 6
    $NUM_SCENES  = 5
    $TRACK_W  = [int](($W - $SCENE_W) / $NUM_TRACKS)
    $SCENE_H  = [int]($CLIP_AREA_H / $NUM_SCENES)
    $CLIP_PAD = 2

    # Track accent colours
    $trackCols = @('#FF4444','#FF8800','#FFDD00','#44CC44','#00AAFF','#AA44FF')

    # Sample session state — mix of playing, queued, empty
    $trackNames  = @('KICK','BASS','SYNTH','KEYS','PERC','PAD')
    $sceneNames  = @('INTRO','VERSE','CHORUS','BRIDGE','OUTRO')

    # [track][scene] clip data: (name, playing, hasClip)
    $clips = @(
        @(@('K-INTRO',$true,$true),  @('K-VERSE',$false,$true), @('K-CHORUS',$false,$true), @('',$false,$false), @('',$false,$false)),
        @(@('BASS-DRP',$true,$true),  @('BASS-V',$false,$true),  @('BASS-C',$false,$true),   @('BASS-BRG',$false,$true), @('',$false,$false)),
        @(@('PAD SWEEP',$false,$true),@('SYNTH-V',$false,$true), @('LEAD',$true,$true),       @('SYNTH-B',$false,$true),  @('',$false,$false)),
        @(@('',$false,$false),        @('KEYS-V',$false,$true),  @('KEYS-C',$true,$true),     @('KEYS-B',$false,$true),   @('KEYS-OUT',$false,$true)),
        @(@('PERC-I',$false,$true),   @('PERC-V',$false,$true),  @('PERC-C',$false,$true),    @('',$false,$false),        @('',$false,$false)),
        @(@('',$false,$false),        @('',$false,$false),        @('STRINGS',$true,$true),    @('PAD-B',$false,$true),    @('LONG PAD',$false,$true))
    )

    # Level & peak data per track (0.0–1.0)
    $levels = @(0.72, 0.58, 0.45, 0.62, 0.38, 0.81)
    $peaks  = @(0.85, 0.70, 0.60, 0.78, 0.55, 0.88)

    # ── Header ────────────────────────────────────────────────────────────────
    $null = $sb.AppendLine("<rect x='0' y='0' width='$W' height='$HEADER_H' fill='#0A1A10'/>")
    $null = $sb.AppendLine("<text x='10' y='24' fill='#00CC44' font-family='Courier New,monospace' font-size='14px' font-weight='bold'>ABLETON LIVE</text>")
    # Playing indicator
    $null = $sb.AppendLine("<polygon points='130,12 130,26 143,19' fill='#00FF55'/>")
    $null = $sb.AppendLine("<text x='150' y='24' fill='#CCCCCC' font-family='Courier New,monospace' font-size='13px'>PLAYING</text>")
    # BPM
    $null = $sb.AppendLine("<text x='360' y='14' fill='#888888' font-family='Courier New,monospace' font-size='9px' text-anchor='middle'>BPM</text>")
    $null = $sb.AppendLine("<text x='360' y='28' fill='#FFFFFF' font-family='Courier New,monospace' font-size='15px' font-weight='bold' text-anchor='middle'>128.00</text>")
    # Beat position
    $null = $sb.AppendLine("<text x='450' y='14' fill='#888888' font-family='Courier New,monospace' font-size='9px' text-anchor='middle'>BAR.BEAT</text>")
    $null = $sb.AppendLine("<text x='450' y='28' fill='#AAFFAA' font-family='Courier New,monospace' font-size='13px' font-weight='bold' text-anchor='middle'>005.03</text>")
    # OSC port indicator
    $null = $sb.AppendLine("<text x='$(fmt ($W-8))' y='17' fill='#00AAFF' font-family='Courier New,monospace' font-size='8px' text-anchor='end'>OSC :11000</text>")
    $null = $sb.AppendLine("<circle cx='$(fmt ($W-124))' cy='13' r='4' fill='#00CC44'/>")

    # ── Scene name column + Track name headers ────────────────────────────────
    $null = $sb.AppendLine("<rect x='0' y='$HEADER_H' width='$SCENE_W' height='$TRKNAME_H' fill='#181818'/>")
    $null = $sb.AppendLine("<text x='$(fmt ($SCENE_W*0.5))' y='$(fmt ($HEADER_H+15))' fill='#555555' font-family='Courier New,monospace' font-size='8px' text-anchor='middle'>SCENES</text>")

    for ($t = 0; $t -lt $NUM_TRACKS; $t++) {
        $tx = $SCENE_W + $t * $TRACK_W
        $tc = $trackCols[$t]
        $null = $sb.AppendLine("<rect x='$(fmt $tx)' y='$HEADER_H' width='$(fmt $TRACK_W)' height='$TRKNAME_H' fill='${tc}22'/>")
        $null = $sb.AppendLine("<rect x='$(fmt $tx)' y='$HEADER_H' width='$(fmt $TRACK_W)' height='2' fill='$tc'/>")
        $null = $sb.AppendLine("<text x='$(fmt ($tx+$TRACK_W*0.5))' y='$(fmt ($HEADER_H+15))' fill='$tc' font-family='Courier New,monospace' font-size='9px' font-weight='bold' text-anchor='middle'>$($trackNames[$t])</text>")
    }

    # ── Clip grid ─────────────────────────────────────────────────────────────
    for ($s = 0; $s -lt $NUM_SCENES; $s++) {
        $sy   = $CLIP_AREA_Y + $s * $SCENE_H
        $sEven = if ($s % 2 -eq 0) { '#151515' } else { '#131313' }
        $null = $sb.AppendLine("<rect x='0' y='$(fmt $sy)' width='$W' height='$(fmt $SCENE_H)' fill='$sEven'/>")

        # Scene name cell
        $null = $sb.AppendLine("<rect x='0' y='$(fmt $sy)' width='$SCENE_W' height='$(fmt $SCENE_H)' fill='#161616'/>")
        $null = $sb.AppendLine("<rect x='0' y='$(fmt $sy)' width='$SCENE_W' height='$(fmt $SCENE_H)' fill='none' stroke='#222222' stroke-width='1'/>")
        $null = $sb.AppendLine("<text x='$(fmt ($SCENE_W*0.5))' y='$(fmt ($sy+$SCENE_H*0.5+4))' fill='#666666' font-family='Courier New,monospace' font-size='8px' text-anchor='middle'>$($sceneNames[$s])</text>")

        # Clip cells per track
        for ($t = 0; $t -lt $NUM_TRACKS; $t++) {
            $cx2 = $SCENE_W + $t * $TRACK_W + $CLIP_PAD
            $cy2 = $sy + $CLIP_PAD
            $cw  = $TRACK_W - $CLIP_PAD * 2
            $ch  = $SCENE_H - $CLIP_PAD * 2
            $cd   = $clips[$t][$s]
            $cname = $cd[0]; $cplay = $cd[1]; $chas = $cd[2]
            dawAdd $sb (dawClip $cx2 $cy2 $cw $ch $cname $cplay $chas $trackCols[$t])
        }
    }

    # ── Level meters ──────────────────────────────────────────────────────────
    $METER_Y = $H - $METER_H
    $null = $sb.AppendLine("<rect x='0' y='$METER_Y' width='$W' height='$METER_H' fill='#0C0C0C'/>")
    $null = $sb.AppendLine("<rect x='0' y='$METER_Y' width='$W' height='1' fill='#2A2A2A'/>")

    # Scene col spacer
    $null = $sb.AppendLine("<rect x='0' y='$METER_Y' width='$SCENE_W' height='$METER_H' fill='#0A0A0A'/>")

    for ($t = 0; $t -lt $NUM_TRACKS; $t++) {
        $mx = $SCENE_W + $t * $TRACK_W
        $tc = $trackCols[$t]
        # Track name mini label
        $null = $sb.AppendLine("<text x='$(fmt ($mx+$TRACK_W*0.5))' y='$(fmt ($METER_Y+10))' fill='#444444' font-family='Courier New,monospace' font-size='7px' text-anchor='middle'>$($trackNames[$t])</text>")
        # Two meter bars (L+R) side by side in the track column
        $mBarW = [int](($TRACK_W - 14) / 2)
        $mL    = $mx + 5
        $mR    = $mL + $mBarW + 4
        $mY    = $METER_Y + 13
        $mH    = $METER_H - 16
        dawAdd $sb (dawMeter $mL $mY $mBarW $mH ($levels[$t]*0.95) ($peaks[$t]*0.93) $tc)
        dawAdd $sb (dawMeter $mR $mY $mBarW $mH ($levels[$t])      ($peaks[$t])      $tc)
    }

    $null = $sb.AppendLine("</svg>")
    $sb.ToString() | Set-Content $file -Encoding UTF8
    Write-Host "$(Split-Path $file -Leaf) done"
}

Write-DawSvg "$OutDir\daw-session.svg"

# ─────────────────────────────────────────────────────────────────────────────
# 11. WAVEFORM (AUDIO)   1280 × 200
#     Rolling stereo waveform — L above centre, R below.
#     Amplitude-based colour gradient: green → amber → red.
# ─────────────────────────────────────────────────────────────────────────────
function Write-WaveformSvg($file) {
    $W = 1280; $H = 200; $cY = $H / 2

    $sb = [System.Text.StringBuilder]::new()
    $null = $sb.AppendLine("<?xml version='1.0' encoding='UTF-8'?>")
    $null = $sb.AppendLine("<svg xmlns='http://www.w3.org/2000/svg' width='$W' height='$H' viewBox='0 0 $W $H'>")

    # Background
    $null = $sb.AppendLine("<rect width='$W' height='$H' fill='#0A0A0A'/>")

    # Faint centre line
    $null = $sb.AppendLine("<line x1='0' y1='$cY' x2='$W' y2='$cY' stroke='#2A2A2A' stroke-width='1'/>")

    # Simulate two seconds of stereo audio — a DJ track with regular kick transients.
    # The waveform is 1280 columns wide.  Each column is 1 px.
    # Amplitude envelope: slow breathing + periodic kick peaks + random texture.
    $colData = @()
    $numCols = $W
    $pi = [Math]::PI

    for ($x = 0; $x -lt $numCols; $x++) {
        $t = $x / ($numCols - 1)   # 0..1 across the time window

        # Bass/kick envelope: peaks at 0.0, 0.25, 0.5, 0.75, 1.0 (quarter notes)
        $kick = 0.0
        foreach ($beat in @(0.0, 0.25, 0.5, 0.75, 1.0)) {
            $d = [Math]::Abs($t - $beat)
            if ($d -lt 0.04) { $kick = [Math]::Max($kick, 1.0 - $d / 0.04) }
        }
        $kick = $kick * 0.85

        # Mid-frequency body (music underneath)
        $body = 0.28 + 0.10 * [Math]::Sin($t * $pi * 6.0)

        # High-frequency texture (slight random-like variation using fast sine)
        $texture = 0.08 * [Math]::Abs([Math]::Sin($t * $pi * 80.0)) `
                 + 0.04 * [Math]::Abs([Math]::Sin($t * $pi * 130.0))

        $ampL = [Math]::Min(1.0, $kick + $body + $texture)

        # R channel is similar but with slight phase difference → slightly different shape
        $kickR = 0.0
        foreach ($beat in @(0.0, 0.25, 0.5, 0.75, 1.0)) {
            $d = [Math]::Abs($t - $beat - 0.008)
            if ($d -lt 0.04) { $kickR = [Math]::Max($kickR, 1.0 - $d / 0.04) }
        }
        $kickR = $kickR * 0.82
        $bodyR = 0.27 + 0.09 * [Math]::Sin($t * $pi * 6.0 + 0.3)
        $textR = 0.07 * [Math]::Abs([Math]::Sin($t * $pi * 78.0)) `
               + 0.05 * [Math]::Abs([Math]::Sin($t * $pi * 123.0))
        $ampR = [Math]::Min(1.0, $kickR + $bodyR + $textR)

        $colData += ,@($ampL, $ampR)
    }

    # Helper: map amplitude 0..1 to hex colour green→amber→red
    function wfColor($amp) {
        if ($amp -lt 0.6) {
            $t = $amp / 0.6
            # green #00CC44 → amber #DD8800
            $r = [int](0x00 + (0xDD - 0x00) * $t)
            $g = [int](0xCC + (0x88 - 0xCC) * $t)
            $b = [int](0x44 + (0x00 - 0x44) * $t)
        } else {
            $t = ($amp - 0.6) / 0.4
            # amber #DD8800 → red #FF2200
            $r = [int](0xDD + (0xFF - 0xDD) * $t)
            $g = [int](0x88 + (0x22 - 0x88) * $t)
            $b = 0
        }
        return '#{0:X2}{1:X2}{2:X2}' -f $r,$g,$b
    }

    # Draw waveform columns — group adjacent same-colour columns into rectangles
    # for smaller SVG output, but simple per-column rects are fine for a mockup.
    for ($x = 0; $x -lt $numCols; $x++) {
        $ampL = $colData[$x][0]
        $ampR = $colData[$x][1]

        $bHL = [Math]::Max(1, [int]($ampL * $cY))
        $bHR = [Math]::Max(1, [int]($ampR * $cY))
        $colL = wfColor $ampL
        $colR = wfColor $ampR

        # L channel — grows upward from centre
        $yL = $cY - $bHL
        $null = $sb.AppendLine("<rect x='$x' y='$(fmt $yL)' width='1' height='$bHL' fill='$colL'/>")

        # R channel — grows downward from centre
        $null = $sb.AppendLine("<rect x='$x' y='$cY' width='1' height='$bHR' fill='$colR'/>")
    }

    # Channel labels
    $null = $sb.AppendLine("<text x='6' y='14' fill='#40804060' font-family='Courier New,monospace' font-size='9px'>L</text>")
    $null = $sb.AppendLine("<text x='6' y='$(fmt ($cY + 20))' fill='#40804060' font-family='Courier New,monospace' font-size='9px'>R</text>")

    # Time ruler at the bottom — every 0.25 s tick
    $null = $sb.AppendLine("<rect x='0' y='$(fmt ($H-10))' width='$W' height='1' fill='#1E1E1E'/>")
    for ($tick = 0; $tick -le 8; $tick++) {
        $tx = [int]($tick * $W / 8)
        $label = "$(fmt ($tick * 0.25))s"
        $null = $sb.AppendLine("<line x1='$tx' y1='$(fmt ($H-10))' x2='$tx' y2='$H' stroke='#333333' stroke-width='1'/>")
        if ($tick -lt 8) {
            $null = $sb.AppendLine("<text x='$(fmt ($tx+2))' y='$(fmt ($H-1))' fill='#555555' font-family='Courier New,monospace' font-size='7px'>$label</text>")
        }
    }

    # Info strip top-right: source name + time window
    $null = $sb.AppendLine("<text x='$(fmt ($W-6))' y='12' fill='#3A3A3A' font-family='Courier New,monospace' font-size='8px' text-anchor='end'>WAVEFORM · 2.0s · STEREO</text>")

    $null = $sb.AppendLine("</svg>")
    $sb.ToString() | Set-Content $file -Encoding UTF8
    Write-Host "$(Split-Path $file -Leaf) done"
}

Write-WaveformSvg "$OutDir\waveform.svg"

Write-Host "All mockup SVGs written to $OutDir"

