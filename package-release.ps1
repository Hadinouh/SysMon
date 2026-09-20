param(
    [switch]$RunTests,
    [switch]$FrameworkDependentSensors
)

$ErrorActionPreference = "Stop"
Push-Location $PSScriptRoot
try {
    $versionMatch = Select-String -Path "Version.h" -Pattern '^#define\s+SYSMON_VERSION_STRING\s+"([^"]+)"$'
    if (-not $versionMatch) { throw "Could not read version from Version.h" }
    $version = $versionMatch.Matches[0].Groups[1].Value

    $distRoot = Join-Path $PSScriptRoot "dist"
    $releaseName = "SysMon-v$version-Windows-x64"
    $releaseDir = Join-Path $distRoot $releaseName
    $zipPath = Join-Path $distRoot "$releaseName.zip"

    if (Test-Path $releaseDir) { Remove-Item $releaseDir -Recurse -Force }
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null

    $buildArgs = @{ OutputPath = (Join-Path $releaseDir "SysMon.exe") }
    if ($RunTests) { $buildArgs.RunTests = $true }
    & (Join-Path $PSScriptRoot "build.ps1") @buildArgs

    $assets = @(
        "logo.png", "dashboard.png", "processes.png", "performance.png", "tools.png", "settings.png",
        "cpu.png", "memory.png", "disk.png", "gpu.png", "temp.png", "network.png",
        "sidebar_overview.png", "sidebar_cpu.png", "sidebar_memory.png", "sidebar_disk.png",
        "sidebar_gpu.png", "sidebar_network.png", "sidebar_temperatures.png", "sidebar_processes.png",
        "sidebar_systeminfo.png", "sidebar_settings.png",
        "sysinfo_os.png", "sysinfo_cpu.png", "sysinfo_gpu.png", "sysinfo_memory.png",
        "sysinfo_storage.png", "sysinfo_uptime.png",
        "SysMon-help.txt"
    )

    foreach ($asset in $assets) {
        if (-not (Test-Path $asset)) { throw "Missing runtime asset: $asset" }
        Copy-Item $asset -Destination $releaseDir -Force
    }

    $dotnet = Get-Command dotnet -ErrorAction Stop
    $sensorOut = Join-Path $releaseDir "SysMonSensors"
    $publishArgs = @(
        "publish", "SysMonSensors\SysMonSensors.csproj",
        "-c", "Release",
        "-r", "win-x64",
        "-o", $sensorOut
    )
    if ($FrameworkDependentSensors) {
        $publishArgs += @("--self-contained", "false")
    } else {
        $publishArgs += @("--self-contained", "true")
    }
    & $dotnet.Source @publishArgs
    if ($LASTEXITCODE -ne 0) { throw "SysMonSensors publish failed" }

    Copy-Item "SysMonSensors\ThirdParty\LibreHardwareMonitor\LICENSE" -Destination (Join-Path $sensorOut "LibreHardwareMonitor-LICENSE.txt") -Force
    Copy-Item "SysMonSensors\ThirdParty\LibreHardwareMonitor\THIRD-PARTY-NOTICES.txt" -Destination $sensorOut -Force

    Copy-Item "RELEASE-NOTES-v0.9.0.md" -Destination (Join-Path $releaseDir "RELEASE-NOTES.txt") -Force

    Compress-Archive -Path (Join-Path $releaseDir "*") -DestinationPath $zipPath -CompressionLevel Optimal

    Write-Host ""
    Write-Host "Release package created:" -ForegroundColor Green
    Write-Host $zipPath
    Write-Host "Upload this ZIP to the GitHub v$version release."
}
finally {
    Pop-Location
}
