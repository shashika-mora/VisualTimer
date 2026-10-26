# Builds a single static VisualTimer.exe with MinGW-w64 (MSYS2 ucrt64 or mingw64).
# Usage:  ./build.ps1
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force build | Out-Null

if (-not (Test-Path res\app.ico)) { & "$PSScriptRoot\tools\make-icon.ps1" }

windres res\app.rc -O coff -o build\app.res.o
if ($LASTEXITCODE -ne 0) { throw "windres failed" }

g++ -O2 -std=c++17 -Wall -Wextra -Wno-missing-field-initializers -municode -mwindows -static -static-libgcc -static-libstdc++ `
    src\main.cpp build\app.res.o -o build\VisualTimer.exe -s `
    -lgdiplus -lgdi32 -luser32 -lshell32 -ldwmapi -lole32 -loleaut32 -luuid
if ($LASTEXITCODE -ne 0) { throw "g++ failed" }

Write-Host "OK -> build\VisualTimer.exe ($([math]::Round((Get-Item build\VisualTimer.exe).Length / 1KB)) KB)"
