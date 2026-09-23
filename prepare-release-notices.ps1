param([Parameter(Mandatory=$true)][string]$ReleaseDir)
$ErrorActionPreference = 'Stop'
$sensorOut = Join-Path $ReleaseDir 'SysMonSensors'
$deps = Get-Content (Join-Path $sensorOut 'SysMonSensors.deps.json') -Raw | ConvertFrom-Json
$known = @('SysMonSensors/1.0.0','LibreHardwareMonitorLib/0.9.6',
    'runtimepack.Microsoft.NETCore.App.Runtime.win-x64/10.0.8',
    'BlackSharp.Core/1.2.0','DiskInfoToolkit/2.1.4','RAMSPDToolkit-NDD/1.6.1',
    'HidSharp/2.6.4','Mono.Posix.NETStandard/1.0.0','System.CodeDom/10.0.12',
    'System.Formats.Nrbf/10.0.12','System.IO.Ports/10.0.12',
    'System.Management/10.0.12','System.Resources.Extensions/10.0.12')
$resolved = @($deps.libraries.PSObject.Properties.Name | Sort-Object)
foreach ($name in $resolved) {
    if ($name -notin $known) { throw "Review THIRD-PARTY-NOTICES.txt for new dependency: $name" }
}
$resolved | ConvertTo-Json | Set-Content (Join-Path $ReleaseDir 'DEPENDENCIES.json') -Encoding UTF8
$nugetRoot = $env:NUGET_PACKAGES
if (-not $nugetRoot) { $nugetRoot = Join-Path $env:USERPROFILE '.nuget/packages' }
$metadataDir = Join-Path $ReleaseDir 'licenses/packages'
New-Item -ItemType Directory -Force $metadataDir | Out-Null
foreach ($name in $resolved) {
    if ($name -like 'SysMonSensors/*' -or $name -like 'LibreHardwareMonitorLib/*') { continue }
    $package = $name -replace '^runtimepack\.', ''
    $root = Join-Path $nugetRoot $package.ToLowerInvariant()
    Get-ChildItem $root -Filter '*.nuspec' | Copy-Item -Destination $metadataDir
}
$sourceOut = Join-Path $ReleaseDir 'sources'
New-Item -ItemType Directory -Force $sourceOut | Out-Null
$vendor = Join-Path $PSScriptRoot 'SysMonSensors/ThirdParty/LibreHardwareMonitor'
Get-ChildItem -LiteralPath $vendor -Recurse -File -Force | ForEach-Object {
    $relative = $_.FullName.Substring($vendor.Length + 1)
    if ($relative -match '(^|[\\/])(bin|obj|\.git|\.vs)([\\/]|$)' -or
        $_.Extension -in @('.pdb','.user','.suo')) { return }
    $destination = Join-Path $sourceOut ('LibreHardwareMonitor/' + $relative)
    New-Item -ItemType Directory -Force (Split-Path $destination) | Out-Null
    Copy-Item -LiteralPath $_.FullName -Destination $destination
}
$archives = @{
    'BlackSharp.Core-1.2.0' = 'Blacktempel/BlackSharp/zip/e3383d014620777a561c939efe3f27ed4f72bc04'
    'DiskInfoToolkit-2.1.4' = 'Blacktempel/DiskInfoToolkit/zip/a6ea726b6118d4469d1228e19b5db0f3437864dc'
    'RAMSPDToolkit-NDD-1.6.1' = 'Blacktempel/RAMSPDToolkit/zip/0ea51855144eef82787f983004fb8ce7346f8227'
    'PawnIO.Modules-0.2.11' = 'namazso/PawnIO.Modules/zip/refs/tags/0.2.11'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($entry in $archives.GetEnumerator()) {
    $outFile = Join-Path $sourceOut ($entry.Key + '.zip')
    Invoke-WebRequest -UseBasicParsing ('https://codeload.github.com/' + $entry.Value) -OutFile $outFile
    $archive = [IO.Compression.ZipFile]::OpenRead($outFile)
    try { if ($archive.Entries.Count -eq 0) { throw "Empty source archive: $outFile" } }
    finally { $archive.Dispose() }
}
@(& g++ --version; & dotnet --version) | Set-Content (Join-Path $ReleaseDir 'BUILD-TOOLS.txt') -Encoding UTF8
if (Get-ChildItem -LiteralPath $ReleaseDir -Recurse -Filter '*.pdb') {
    throw 'Release contains debug symbols; rebuild without them.'
}
$privatePaths = @($PSScriptRoot, $env:USERPROFILE) | Where-Object { $_ }
Get-ChildItem -LiteralPath $ReleaseDir -Recurse -File | Where-Object { $_.Extension -in @('.dll','.exe') } | ForEach-Object {
    $bytes = [IO.File]::ReadAllBytes($_.FullName)
    $ascii = [Text.Encoding]::UTF8.GetString($bytes)
    $wide = [Text.Encoding]::Unicode.GetString($bytes)
    foreach ($path in $privatePaths) {
        foreach ($variant in @($path, $path.Replace('\','/'))) {
            if ($ascii.IndexOf($variant,[StringComparison]::OrdinalIgnoreCase) -ge 0 -or
                $wide.IndexOf($variant,[StringComparison]::OrdinalIgnoreCase) -ge 0) {
                throw "Private build path embedded in $($_.Name)"
            }
        }
    }
}
Write-Host 'Verified dependency inventory, corresponding sources and absence of private build paths/PDBs.'
