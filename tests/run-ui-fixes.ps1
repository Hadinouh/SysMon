$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot)
try {
    $compiler = (Get-Command g++).Source
    $sources = @('Stats.cpp','Processes.cpp','UI.cpp','Settings.cpp','Tray.cpp','Widget.cpp')
    $libraries = @('-lgdiplus','-lgdi32','-lpsapi','-lcomctl32','-lshell32','-lole32','-loleaut32','-luuid','-lwbemuuid','-liphlpapi','-lws2_32','-lsetupapi','-lcfgmgr32','-lpdh','-lwlanapi','-lpowrprof','-ldwmapi','-ltaskschd','-lwinhttp','-lversion')
    & $compiler -std=c++17 -O2 -static -I. tests/process_navigation_test.cpp @sources -o tests/process_navigation_test.exe @libraries
    if ($LASTEXITCODE -ne 0) { throw 'Navigation test compilation failed' }
    & .\tests\process_navigation_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Navigation test failed' }
    & $compiler -std=c++17 -O2 -static -I. tests/general_settings_test.cpp Main.cpp @sources -o tests/general_settings_test.exe @libraries
    if ($LASTEXITCODE -ne 0) { throw 'Settings test compilation failed' }
    & .\tests\general_settings_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Settings test failed' }
} finally { Pop-Location }
