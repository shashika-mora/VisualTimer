# Generates res/app.ico (multi-size, PNG-compressed frames) using System.Drawing.
# Run once from the repo root:  powershell -ExecutionPolicy Bypass -File tools\make-icon.ps1
Add-Type -AssemblyName System.Drawing

$sizes = 16, 24, 32, 48, 64, 128, 256
$frames = @()
foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $rim  = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 39, 39, 46))
    $face = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 248, 248, 250))
    $red  = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 239, 68, 68))
    $dot  = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 39, 39, 46))

    $g.FillEllipse($rim, 0, 0, $s - 1, $s - 1)
    $m = [Math]::Max(1.0, $s * 0.08)
    $g.FillEllipse($face, $m, $m, $s - 1 - 2 * $m, $s - 1 - 2 * $m)

    # tick marks (only when there is room for them)
    if ($s -ge 48) {
        $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 120, 120, 130)), ([single]($s / 64.0 * 2))
        $c = ($s - 1) / 2.0
        for ($i = 0; $i -lt 12; $i++) {
            $a = ($i * 30 - 90) * [Math]::PI / 180
            $r1 = $c - $m - $s * 0.03; $r2 = $r1 - $s * 0.07
            $g.DrawLine($pen, [single]($c + [Math]::Cos($a) * $r1), [single]($c + [Math]::Sin($a) * $r1),
                              [single]($c + [Math]::Cos($a) * $r2), [single]($c + [Math]::Sin($a) * $r2))
        }
    }

    $w = $s * 0.66
    $o = ($s - 1 - $w) / 2
    $g.FillPie($red, [single]$o, [single]$o, [single]$w, [single]$w, -90, -250)
    if ($s -ge 32) {
        $d = $s * 0.1
        $g.FillEllipse($dot, [single](($s - 1 - $d) / 2), [single](($s - 1 - $d) / 2), [single]$d, [single]$d)
    }
    $g.Dispose()

    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $frames += , @($s, $ms.ToArray())
    $bmp.Dispose()
}

$out = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $out
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$frames.Count)
$offset = 6 + 16 * $frames.Count
foreach ($f in $frames) {
    $s = $f[0]; $data = $f[1]
    $b = if ($s -ge 256) { 0 } else { $s }
    $bw.Write([byte]$b); $bw.Write([byte]$b); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([UInt16]1); $bw.Write([UInt16]32)
    $bw.Write([UInt32]$data.Length); $bw.Write([UInt32]$offset)
    $offset += $data.Length
}
foreach ($f in $frames) { $bw.Write([byte[]]$f[1]) }
$bw.Flush()
New-Item -ItemType Directory -Force res | Out-Null
[System.IO.File]::WriteAllBytes((Join-Path (Get-Location) 'res\app.ico'), $out.ToArray())
Write-Host "Wrote res\app.ico"
