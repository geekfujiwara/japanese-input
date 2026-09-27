<#
.SYNOPSIS
    Builds the Astelio IME setup exe and MSI for one architecture with WiX Toolset v5.
.DESCRIPTION
    Makes AstelioIME-<version>-<arch>-setup.exe (the wizard, for people) and AstelioIME-<version>-<arch>.msi (for
    the Microsoft Store and silent installs).
    Expects the CI artifacts laid out like "gh run download": <Artifacts>\astelio-tip-windows-<arch>\astelio_tip.dll,
    <Artifacts>\astelio-tip-windows-x86\astelio_tip.dll and <Artifacts>\astelio-dictionary\system.dic. The settings app
    (platform/windows/settings/Astelio.Settings) is published here for the same architecture unless -SettingsApp
    gives an AstelioSettings.exe.
    WiX is downloaded from nuget.org at a pinned version and checked against SHA-256. No WiX extension is used, so
    nothing of WiX (MS-RL) goes into the MSI. Needs the .NET SDK (WiX 5 targets .NET 6 and runs on a newer runtime
    through roll-forward). -Version defaults to the project version in CMakeLists.txt.
.EXAMPLE
    ./tools/Build-Installer.ps1 -Arch arm64 -Artifacts artifacts/tip
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('arm64', 'x64')][string]$Arch,
    [string]$Artifacts = (Join-Path $PSScriptRoot '..\artifacts\tip'),
    [ValidatePattern('^(\d+\.\d+\.\d+)?$')][string]$Version = '',
    [string]$SettingsApp = '',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\artifacts\installer')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $Version) {
    $Version = [regex]::Match((Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw),
        'project\(AstelioIME VERSION (\d+\.\d+\.\d+)').Groups[1].Value
}
$work = Join-Path $root 'build\installer'
$packages = @(
    @{ Id = 'wix'; Sha256 = 'f30ef0c74e2a986126539c5780be93ac24e8136eaf723b1937b26272703ae173' }
)
$wixVersion = '5.0.2'

function Get-Package([hashtable]$Package) {
    $folder = Join-Path $work "$($Package.Id).$wixVersion"
    if (Test-Path (Join-Path $folder '.verified')) {
        return $folder
    }
    New-Item -ItemType Directory -Force -Path $work | Out-Null
    $file = Join-Path $work "$($Package.Id).$wixVersion.nupkg"
    Invoke-WebRequest "https://api.nuget.org/v3-flatcontainer/$($Package.Id)/$wixVersion/$($Package.Id).$wixVersion.nupkg" -OutFile $file
    $hash = (Get-FileHash $file -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $Package.Sha256) {
        throw "SHA-256 of $($Package.Id) $wixVersion is $hash, expected $($Package.Sha256)"
    }
    if (Test-Path $folder) {
        Remove-Item -LiteralPath $folder -Recurse -Force
    }
    Expand-Archive -LiteralPath $file -DestinationPath $folder
    New-Item -ItemType File -Path (Join-Path $folder '.verified') | Out-Null
    return $folder
}

$nativeTip = Join-Path $Artifacts "astelio-tip-windows-$Arch\astelio_tip.dll"
$x86Tip = Join-Path $Artifacts 'astelio-tip-windows-x86\astelio_tip.dll'
$dictionary = Join-Path $Artifacts 'astelio-dictionary'
foreach ($required in $nativeTip, $x86Tip, (Join-Path $dictionary 'system.dic')) {
    if (-not (Test-Path $required)) {
        throw "Not found: $required"
    }
}

if (-not $SettingsApp) {
    $settingsOutput = Join-Path $work "settings-$Arch"
    & dotnet publish (Join-Path $root 'platform\windows\settings\Astelio.Settings\Astelio.Settings.csproj') `
        -c Release -r "win-$Arch" -p:Version=$Version -o $settingsOutput | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "dotnet publish of the settings app failed with exit code $LASTEXITCODE"
    }
    $SettingsApp = Join-Path $settingsOutput 'AstelioSettings.exe'
}
if (-not (Test-Path $SettingsApp)) {
    throw "Not found: $SettingsApp"
}

$wix = Get-Package $packages[0]
$wixDll = Join-Path $wix 'tools\net6.0\any\wix.dll'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$env:DOTNET_ROLL_FORWARD = 'Major'

function Build-Msi([string]$Output, [string]$Compressed, [string]$Intermediate) {
    $arguments = @(
        $wixDll, 'build', (Join-Path $root 'platform\windows\installer\Package.wxs'),
        '-arch', $Arch,
        '-d', "Version=$Version", '-d', "Arch=$Arch",
        '-d', "NativeTip=$((Resolve-Path $nativeTip).Path)", '-d', "X86Tip=$((Resolve-Path $x86Tip).Path)",
        '-d', "DictionaryDir=$((Resolve-Path $dictionary).Path)", '-d', "RepositoryRoot=$root",
        '-d', "SettingsApp=$((Resolve-Path $SettingsApp).Path)", '-d', "SettingsCompressed=$Compressed",
        '-d', "IconFile=$(Join-Path $root 'assets\icon\AstelioIME.ico')",
        '-intermediatefolder', (Join-Path $work $Intermediate), '-o', $Output
    )
    & dotnet @arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "wix build failed with exit code $LASTEXITCODE"
    }
    # ICE43 / ICE57 treat the Start menu as a user profile folder; this package is per machine (ALLUSERS=1).
    & dotnet $wixDll msi validate -sice ICE43 -sice ICE57 $Output | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "The MSI did not pass the ICE validation (exit code $LASTEXITCODE)"
    }
}

# The MSI on its own (Microsoft Store, silent installs): everything inside.
$output = Join-Path $OutputDirectory "AstelioIME-$Version-$Arch.msi"
Build-Msi $output 'default' "obj-$Arch"
Write-Host "Built $output"

# The setup exe: the settings app with an MSI that leaves the app out appended (Setup/SetupPayload.cs). WiX lays the
# app out next to that MSI where Windows Installer looks for it; the setup writes itself to the same place.
$setupWork = Join-Path $work "setup-$Arch"
if (Test-Path $setupWork) {
    Remove-Item -LiteralPath $setupWork -Recurse -Force
}
$setupMsi = Join-Path $setupWork 'AstelioIME.msi'
Build-Msi $setupMsi 'no' "obj-setup-$Arch"
$laidOut = Get-ChildItem -LiteralPath $setupWork -Recurse -File -Filter 'AstelioSettings.exe' | Select-Object -First 1
if (-not $laidOut) {
    throw "WiX did not lay out AstelioSettings.exe in $setupWork"
}
$appPath = [IO.Path]::GetRelativePath($setupWork, $laidOut.FullName)
$setup = Join-Path $OutputDirectory "AstelioIME-$Version-$Arch-setup.exe"
Copy-Item -LiteralPath $SettingsApp -Destination $setup -Force
$stream = [IO.File]::Open($setup, 'Append', 'Write')
try {
    $msiBytes = [IO.File]::ReadAllBytes($setupMsi)
    $pathBytes = [Text.Encoding]::UTF8.GetBytes($appPath)
    $stream.Write($msiBytes, 0, $msiBytes.Length)
    $stream.Write($pathBytes, 0, $pathBytes.Length)
    $stream.Write([BitConverter]::GetBytes([int]$pathBytes.Length), 0, 4)
    $stream.Write([BitConverter]::GetBytes([long]$msiBytes.Length), 0, 8)
    $magic = [Text.Encoding]::ASCII.GetBytes('ASTSETUP')
    $stream.Write($magic, 0, $magic.Length)
} finally {
    $stream.Dispose()
}
Write-Host "Built $setup"
