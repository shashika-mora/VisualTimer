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

# Check if gh is logged in
$ErrorActionPreference = 'Continue'
$null = gh auth status 2>&1
$ghOk = ($LASTEXITCODE -eq 0)
$ErrorActionPreference = 'Stop'
if ($ghOk) {
    git push origin main --tags
    gh release create $Version dist\VisualTimer.exe $zip `
        --title "Visual Timer $Version" `
        --notes "Native Windows visual countdown timer for ADHD time awareness. Single portable executable (`VisualTimer.exe`), no installer required."
} else {
    Write-Host ""
    Write-Host "Note: GitHub CLI (gh) is not logged in. To publish the release from terminal:"
    Write-Host "  gh auth login"
    Write-Host "  gh release create $Version dist\VisualTimer.exe $zip --title `"Visual Timer $Version`" --generate-notes"
    Write-Host "Or upload dist\VisualTimer.exe and $zip directly at:"
    Write-Host "  https://github.com/shashika-mora/VisualTimer/releases/new?tag=$Version"
}
