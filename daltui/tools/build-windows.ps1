# Native build in Windows PowerShell, with Rust and VS C++ Build Tools installed.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$Project = Split-Path $PSScriptRoot -Parent
$Target = 'x86_64-pc-windows-msvc'
Push-Location $Project
try {
    & rustup target add $Target
    if ($LASTEXITCODE -ne 0) { throw 'Failed to install the Rust Windows target' }
    & cargo build --release --locked --target $Target --target-dir target
    if ($LASTEXITCODE -ne 0) { throw 'Windows build failed; check Rust and the MSVC C++ linker/SDK' }
    $Folder = Join-Path $Project 'target\windows-x64\daltui-windows-x64'
    New-Item -ItemType Directory -Force -Path $Folder | Out-Null
    Copy-Item "target\$Target\release\daltui.exe" (Join-Path $Folder 'daltui.exe') -Force
    Copy-Item 'tools\windows-README.txt' (Join-Path $Folder 'README.txt') -Force
    $Lines = foreach ($Name in @('daltui.exe', 'README.txt')) {
        $Hash = (Get-FileHash -Algorithm SHA256 (Join-Path $Folder $Name)).Hash.ToLowerInvariant()
        "$Hash  $Name"
    }
    $Lines | Set-Content -Encoding ASCII (Join-Path $Folder 'SHA256SUMS')
    $Archive = Join-Path $Project 'target\windows-x64\daltui-windows-x64.zip'
    Compress-Archive -Path $Folder -DestinationPath $Archive -Force
    Write-Host "EXE: $Folder\daltui.exe"
    Write-Host "ZIP: $Archive"
} finally {
    Pop-Location
}
