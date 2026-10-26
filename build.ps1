# Builds a single static VisualTimer.exe with MinGW-w64 g++ (MSYS2 ucrt64).
New-Item -ItemType Directory -Force build | Out-Null
g++ -O2 -std=c++17 -Wall -municode -mwindows -static -static-libgcc -static-libstdc++ `
    src\main.cpp -o build\VisualTimer.exe `
    -lgdiplus -lgdi32 -luser32 -lshell32 -ldwmapi -lole32 -loleaut32 -luuid -lwinmm
if ($LASTEXITCODE -eq 0) { Write-Host "OK -> build\VisualTimer.exe" }
