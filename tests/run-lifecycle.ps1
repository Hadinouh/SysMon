$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot)
try {
    $compiler = (Get-Command g++).Source
    & $compiler -std=c++17 -O2 -static -I. tests/lifecycle_test.cpp Stats.cpp Processes.cpp -o SysMon.lifecycle-test.exe -lgdiplus -lgdi32 -lpsapi -lcomctl32 -lshell32 -lole32 -loleaut32 -luuid -lwbemuuid -liphlpapi -lws2_32 -lsetupapi -lcfgmgr32 -lpdh -lwlanapi -lpowrprof -ldwmapi -ltaskschd -lwinhttp -lversion
    if ($LASTEXITCODE -ne 0) { throw 'Lifecycle test compilation failed' }
    & .\SysMon.lifecycle-test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Lifecycle verification failed' }
} finally { Pop-Location }
