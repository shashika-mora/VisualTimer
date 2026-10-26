# Builds VisualTimer.exe, packages release artifacts in dist/, pushes to origin,
# and creates a GitHub Release (if `gh` CLI is authenticated).
param(
    [string]$Version = "v1.0.0"
)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

& "$PSScriptRoot\build.ps1"

New-Item -ItemType Directory -Force dist | Out-Null
Copy-Item build\VisualTimer.exe dist\VisualTimer.exe -Force

$zip = "dist\VisualTimer-$Version-win64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path build\VisualTimer.exe, README.md, LICENSE -DestinationPath $zip

Write-Host "Packaged:"
Write-Host "  - dist\VisualTimer.exe"
Write-Host "  - $zip"

# Check if gh is logged in (or use git credential manager token)
$ErrorActionPreference = 'Continue'
$null = gh auth status 2>&1
if ($LASTEXITCODE -ne 0) {
    $c = "protocol=https`nhost=github.com`n" | git credential fill 2>$null
    $m = $c | Select-String '^password=(.+)$'
    if ($m) { $env:GH_TOKEN = $m.Matches.Groups[1].Value }
}
$ErrorActionPreference = 'Stop'
if ($LASTEXITCODE -eq 0 -or $env:GH_TOKEN) {
    git push origin main --tags
    gh release create $Version dist\VisualTimer.exe $zip `
        --title "Visual Timer $Version" `
        --notes "Native Windows visual countdown timer for ADHD time awareness. Single portable executable (VisualTimer.exe), no installer required."
} else {
    Write-Host ""
    Write-Host "Note: GitHub CLI (gh) is not logged in. To publish the release from terminal:"
    Write-Host "  gh auth login"
    Write-Host "  gh release create $Version dist\VisualTimer.exe $zip --title `"Visual Timer $Version`" --generate-notes"
    Write-Host "Or upload dist\VisualTimer.exe and $zip directly at:"
    Write-Host "  https://github.com/shashika-mora/VisualTimer/releases/new?tag=$Version"
}
