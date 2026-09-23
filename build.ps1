param(
    [string]$OutputPath = "SysMon.exe",
    [switch]$RunTests
)
$ErrorActionPreference = "Stop"
Push-Location $PSScriptRoot
try {
    $compiler = (Get-Command g++ -ErrorAction Stop).Source
    $resourceCompiler = Join-Path (Split-Path $compiler) "windres.exe"
    $sources = @("Main.cpp", "Stats.cpp", "Processes.cpp", "UI.cpp", "Settings.cpp", "Tray.cpp", "Widget.cpp")
    $libraries = @("-lgdiplus", "-lgdi32", "-lpsapi", "-lcomctl32", "-lshell32", "-lole32", "-loleaut32", "-luuid", "-lwbemuuid", "-liphlpapi", "-lws2_32", "-lsetupapi", "-lcfgmgr32", "-lpdh", "-lwlanapi", "-lpowrprof", "-ldwmapi", "-ltaskschd", "-lwinhttp", "-lversion")
    $options = @("-std=c++17", "-O2", "-static", "-I.")
    & $resourceCompiler SysMon.rc -O coff -o SysMon.res
    if ($LASTEXITCODE -ne 0) { throw "Resource compilation failed" }
    & $compiler @options -mwindows @sources SysMon.res -o $OutputPath @libraries
    if ($LASTEXITCODE -ne 0) { throw "Application build failed" }
    Write-Host "Built $OutputPath successfully." -ForegroundColor Green
    if ($RunTests) {
        & $compiler @options tests/shutdown_deadline_test.cpp -o tests/shutdown_deadline_test.exe
        if ($LASTEXITCODE -ne 0) { throw "Shutdown deadline test build failed" }
        & ./tests/shutdown_deadline_test.exe
        if ($LASTEXITCODE -ne 0) { throw "Shutdown deadline test failed" }
        & $compiler @options tests/startup_manager_test.cpp -o tests/startup_manager_test.exe @libraries
        if ($LASTEXITCODE -ne 0) { throw "Startup manager test build failed" }
        & ./tests/startup_manager_test.exe
        if ($LASTEXITCODE -ne 0) { throw "Startup manager test failed" }
        & $compiler @options tests/settings_runtime_test.cpp -o tests/settings_runtime_test.exe
        if ($LASTEXITCODE -ne 0) { throw "Settings runtime test build failed" }
        & ./tests/settings_runtime_test.exe
        if ($LASTEXITCODE -ne 0) { throw "Settings runtime test failed" }
        & $compiler @options tests/background_sampler_test.cpp -o tests/background_sampler_test.exe
        if ($LASTEXITCODE -ne 0) { throw "Sampler test build failed" }
        & ./tests/background_sampler_test.exe
        if ($LASTEXITCODE -ne 0) { throw "Sampler test failed" }
        & $compiler @options tests/monitoring_smoke.cpp Stats.cpp Processes.cpp -o tests/monitoring_smoke.exe @libraries
        if ($LASTEXITCODE -ne 0) { throw "Monitoring test build failed" }
        & ./tests/monitoring_smoke.exe
        if ($LASTEXITCODE -ne 0) { throw "Monitoring test failed" }
        & $compiler @options tests/render_smoke.cpp @sources -o tests/render_smoke.exe @libraries
        if ($LASTEXITCODE -ne 0) { throw "Rendering test build failed" }
        & ./tests/render_smoke.exe
        if ($LASTEXITCODE -ne 0) { throw "Rendering test failed" }
        & ./tests/render_smoke.exe light
        if ($LASTEXITCODE -ne 0) { throw "Light-theme rendering test failed" }
        $layoutSources = $sources | Where-Object { $_ -ne "Main.cpp" }
        & $compiler @options tests/responsive_layout_test.cpp @layoutSources -o tests/responsive_layout_test.exe @libraries
        if ($LASTEXITCODE -ne 0) { throw "Layout test build failed" }
        & ./tests/responsive_layout_test.exe
        if ($LASTEXITCODE -ne 0) { throw "Layout test failed" }
        & $compiler @options tests/general_settings_test.cpp @sources -o tests/general_settings_test.exe @libraries
        if ($LASTEXITCODE -ne 0) { throw "General Settings test build failed" }
        & ./tests/general_settings_test.exe
        if ($LASTEXITCODE -ne 0) { throw "General Settings test failed" }
    }
}
finally { Pop-Location }
