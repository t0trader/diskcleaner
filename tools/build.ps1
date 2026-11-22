$ErrorActionPreference = 'Stop'
chcp 65001 | Out-Null
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
[Console]::InputEncoding = [System.Text.Encoding]::UTF8
[System.Environment]::SetEnvironmentVariable('VSLANG', '1033')

& 'D:\soft\vs2026\Common7\Tools\Launch-VsDevShell.ps1' -VsInstallationPath 'D:\soft\vs2026' -Arch amd64 -SkipAutomaticLocation

Set-Location 'D:\srccode\vibecode\diskcleaner'
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build build
exit $LASTEXITCODE
