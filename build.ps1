$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
New-Item -ItemType Directory -Path dist -Force | Out-Null
& g++ -std=c++17 -O2 -Wall -Wextra -municode -mwindows -static -static-libgcc -static-libstdc++ src/main.cpp -o dist/AizoGuard.exe -lgdiplus -lgdi32 -luser32 -lcomdlg32 -lcomctl32 -lwinmm -lole32 -lmfplat -lmfuuid
if ($LASTEXITCODE -ne 0) { throw 'C++ build failed' }
Write-Output "Built: $PSScriptRoot\dist\AizoGuard.exe"


