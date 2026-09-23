param(
    [switch]$RunTests,
    [switch]$FrameworkDependentSensors,
    [ValidatePattern('^(rc[0-9]+)?$')][string]$Candidate = ''
)

$ErrorActionPreference = "Stop"
Push-Location $PSScriptRoot
try {
    $versionMatch = Select-String -Path "Version.h" -Pattern '^#define\s+SYSMON_VERSION_STRING\s+"([^"]+)"$'
    if (-not $versionMatch) { throw "Could not read version from Version.h" }
    $version = $versionMatch.Matches[0].Groups[1].Value

    $distRoot = Join-Path $PSScriptRoot "dist"
    $releaseName = if ($Candidate) { "SysMon-v$version-$Candidate-Windows-x64-unsigned" } else { "SysMon-v$version" }
    $releaseDir = Join-Path $distRoot $releaseName
    $zipPath = Join-Path $distRoot "$releaseName.zip"

    $expectedRoot = [IO.Path]::GetFullPath($distRoot) + [IO.Path]::DirectorySeparatorChar
    $runningRoot = [IO.Path]::GetFullPath($releaseDir) + [IO.Path]::DirectorySeparatorChar
    $running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($runningRoot, [StringComparison]::OrdinalIgnoreCase) }
    if ($running) { throw 'The release folder is in use. Close SysMon or choose another -Candidate before rebuilding.' }
    foreach ($target in @($releaseDir, $zipPath)) {
        $resolved = [IO.Path]::GetFullPath($target)
        if (-not $resolved.StartsWith($expectedRoot, [StringComparison]::OrdinalIgnoreCase)) { throw "Release output escaped dist" }
        if (Test-Path -LiteralPath $target) {
            $item = Get-Item -LiteralPath $target
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Refusing to replace a linked release output" }
            Remove-Item -LiteralPath $target -Recurse -Force
        }
    }
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
        "SysMon-help.txt", "THIRD-PARTY-NOTICES.txt", "LICENSE", "PRIVACY.md",
        "SECURITY.md", "ASSET-NOTICES.md"
    )

    New-Item -ItemType Directory -Path (Join-Path $releaseDir 'logos') -Force | Out-Null
    foreach ($asset in $assets) {
        $source = $asset
        $destination = $releaseDir
        if ([IO.Path]::GetExtension($asset) -eq '.png') {
            $source = Join-Path 'logos' $asset
            $destination = Join-Path $releaseDir 'logos'
        }
        if (-not (Test-Path $source)) { throw "Missing runtime asset: $source" }
        Copy-Item $source -Destination $destination -Force
    }

    Copy-Item -LiteralPath "licenses" -Destination $releaseDir -Recurse -Force
    $dotnet = Get-Command dotnet -ErrorAction Stop
    $sensorOut = Join-Path $releaseDir "SysMonSensors"
    $publishArgs = @(
        "publish", "SysMonSensors\SysMonSensors.csproj",
        "-c", "Release",
        "-r", "win-x64",
        "-o", $sensorOut,
        "-p:DebugSymbols=false", "-p:DebugType=none"
    )
    if ($FrameworkDependentSensors) {
        $publishArgs += @("--self-contained", "false")
    } else {
        $publishArgs += @("--self-contained", "true")
    }
    & $dotnet.Source @publishArgs
    if ($LASTEXITCODE -ne 0) { throw "SysMonSensors publish failed" }

    if (-not $FrameworkDependentSensors) {
        $runtimeConfig = Get-Content -LiteralPath (Join-Path $sensorOut "SysMonSensors.runtimeconfig.json") -Raw | ConvertFrom-Json
        $runtimeVersion = ($runtimeConfig.runtimeOptions.includedFrameworks | Where-Object name -eq 'Microsoft.NETCore.App').version
        $nugetRoot = $env:NUGET_PACKAGES
        if (-not $nugetRoot) { $nugetRoot = Join-Path $env:USERPROFILE '.nuget\packages' }
        $runtimePackage = Join-Path $nugetRoot "microsoft.netcore.app.runtime.win-x64\$runtimeVersion"
        Copy-Item -LiteralPath (Join-Path $runtimePackage 'LICENSE.TXT') -Destination (Join-Path $sensorOut 'DOTNET-LICENSE.txt')
        Copy-Item -LiteralPath (Join-Path $runtimePackage 'THIRD-PARTY-NOTICES.TXT') -Destination (Join-Path $sensorOut 'DOTNET-THIRD-PARTY-NOTICES.txt')
    }
    Copy-Item "SysMonSensors\ThirdParty\LibreHardwareMonitor\LICENSE" -Destination (Join-Path $sensorOut "LibreHardwareMonitor-LICENSE.txt") -Force
    Copy-Item "SysMonSensors\ThirdParty\LibreHardwareMonitor\THIRD-PARTY-NOTICES.txt" -Destination $sensorOut -Force

    Copy-Item "RELEASE-NOTES-v$version.md" -Destination (Join-Path $releaseDir "RELEASE-NOTES.txt") -Force

    & (Join-Path $PSScriptRoot 'prepare-release-notices.ps1') -ReleaseDir $releaseDir

    Get-ChildItem -LiteralPath $releaseDir -Recurse -File | ForEach-Object {
        $hash = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
        "{0}  {1}" -f $hash.Hash, $_.FullName.Substring($releaseDir.Length + 1)
    } | Set-Content -LiteralPath (Join-Path $releaseDir "SHA256SUMS.txt") -Encoding UTF8
    Compress-Archive -Path (Join-Path $releaseDir "*") -DestinationPath $zipPath -CompressionLevel Optimal

    Write-Host ""
    Write-Host "Release package created:" -ForegroundColor Green
    Write-Host $zipPath
    Write-Host "Portable Windows x64 release. This build is unsigned."
}
finally {
    Pop-Location
}
