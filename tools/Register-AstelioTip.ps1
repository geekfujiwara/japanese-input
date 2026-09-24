<#
.SYNOPSIS
    Installs and registers the Astelio IME TIP (TSF text service), or unregisters it.
.DESCRIPTION
    Copies astelio_tip.dll to "Program Files\Astelio IME\<arch>" (writable only by administrators, because the TIP is
    loaded into every app) and registers it with regsvr32. Needs an administrator PowerShell.
    The source folder is laid out like "gh run download": <Path>\astelio-tip-windows-<arch>\astelio_tip.dll and
    <Path>\astelio-dictionary\system.dic (installed to "Program Files\Astelio IME\dictionary").
.EXAMPLE
    ./tools/Register-AstelioTip.ps1 -Path artifacts/tip
.EXAMPLE
    ./tools/Register-AstelioTip.ps1 -Unregister
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$Path = (Join-Path $PSScriptRoot '..\artifacts\tip'),
    [switch]$Unregister
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$installRoot = Join-Path $env:ProgramFiles 'Astelio IME'
$nativeArch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64') { 'arm64' } else { 'x64' }
# Native apps use the native DLL; 32-bit apps load the x86 DLL through the WOW64 registry view.
$targets = @(
    [pscustomobject]@{ Arch = $nativeArch; RegSvr = Join-Path $env:SystemRoot 'System32\regsvr32.exe' }
    [pscustomobject]@{ Arch = 'x86'; RegSvr = Join-Path $env:SystemRoot 'SysWOW64\regsvr32.exe' }
)

function Test-Administrator {
    $principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Invoke-RegSvr([string]$RegSvr, [string[]]$Arguments) {
    $process = Start-Process -FilePath $RegSvr -ArgumentList $Arguments -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        throw "regsvr32 $($Arguments -join ' ') failed with exit code $($process.ExitCode)"
    }
}

# Apps that have the TIP loaded lock the DLL and the dictionary; loaded files can still be renamed, so move them aside first.
function Install-File([string]$Source, [string]$Destination) {
    New-Item -ItemType Directory -Force -Path (Split-Path $Destination) | Out-Null
    if (Test-Path $Destination) {
        Move-Item -LiteralPath $Destination -Destination "$Destination.$([DateTime]::Now.ToString('yyyyMMddHHmmss')).old" -Force
    }
    Copy-Item -LiteralPath $Source -Destination $Destination
    Get-ChildItem -Path (Split-Path $Destination) -Filter '*.old' | Remove-Item -ErrorAction SilentlyContinue
}

if (-not $WhatIfPreference -and -not (Test-Administrator)) {
    throw 'Run this script from PowerShell started as administrator (registering a TIP writes HKLM).'
}

foreach ($target in $targets) {
    $installed = Join-Path $installRoot "$($target.Arch)\astelio_tip.dll"
    if ($Unregister) {
        if ((Test-Path $installed) -and $PSCmdlet.ShouldProcess($installed, 'Unregister')) {
            Invoke-RegSvr $target.RegSvr @('/u', '/s', "`"$installed`"")
            Write-Host "Unregistered $($target.Arch): $installed"
        }
        continue
    }

    $source = Join-Path $Path "astelio-tip-windows-$($target.Arch)\astelio_tip.dll"
    if (-not (Test-Path $source)) {
        throw "Not found: $source (download the CI artifact astelio-tip-windows-$($target.Arch))"
    }
    if ($PSCmdlet.ShouldProcess($installed, "Install from $source and register")) {
        Install-File $source $installed
        Invoke-RegSvr $target.RegSvr @('/s', "`"$installed`"")
        Write-Host "Registered $($target.Arch): $installed"
    }
}

if (-not $Unregister) {
    $dictionarySource = Join-Path $Path 'astelio-dictionary'
    if (-not (Test-Path (Join-Path $dictionarySource 'system.dic'))) {
        throw "Not found: $dictionarySource\system.dic (download the CI artifact astelio-dictionary)"
    }
    $dictionaryTarget = Join-Path $installRoot 'dictionary'
    if ($PSCmdlet.ShouldProcess($dictionaryTarget, "Install the system dictionary from $dictionarySource")) {
        foreach ($file in Get-ChildItem -LiteralPath $dictionarySource -File) {
            Install-File $file.FullName (Join-Path $dictionaryTarget $file.Name)
        }
        Write-Host "Installed the dictionary: $dictionaryTarget"
    }
}

if (-not $Unregister) {
    if ($nativeArch -eq 'arm64') {
        Write-Warning 'x64 apps running under emulation on ARM64 do not get Astelio yet (needs an ARM64X forwarder DLL).'
    }
    Write-Host 'Add "Astelio IME" under Settings > Time & language > Language & region > Japanese > Language options > Keyboards, then sign out and back in if it does not appear.'
}
