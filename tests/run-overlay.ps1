$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot)
try {
    $compiler = (Get-Command g++).Source
    $sources = @('Main.cpp','Stats.cpp','Processes.cpp','UI.cpp','Settings.cpp','Tray.cpp')
    $libraries = @('-lgdiplus','-lgdi32','-lpsapi','-lcomctl32','-lshell32','-lole32','-loleaut32','-luuid','-lwbemuuid','-liphlpapi','-lws2_32','-lsetupapi','-lcfgmgr32','-lpdh','-lwlanapi','-lpowrprof','-ldwmapi','-ltaskschd','-lwinhttp','-lversion')
    & $compiler -std=c++17 -O2 -static -I. tests/overlay_render_test.cpp @sources -o tests/overlay_render_test.exe @libraries
    if ($LASTEXITCODE -ne 0) { throw 'Overlay test compilation failed' }
    & .\tests\overlay_render_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Overlay rendering verification failed' }
    & $compiler -std=c++17 -O2 -static -I. tests/general_settings_test.cpp @sources Widget.cpp -o tests/general_settings_test.exe @libraries
    if ($LASTEXITCODE -ne 0) { throw 'Settings test compilation failed' }
    & .\tests\general_settings_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Settings verification failed' }
} finally { Pop-Location }
