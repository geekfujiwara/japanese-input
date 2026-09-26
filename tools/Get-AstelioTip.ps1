<#
.SYNOPSIS
    Downloads the TIP and the system dictionary built by CI for the latest commit on main.
.DESCRIPTION
    Waits for that commit's CI run to finish (so an older build is never picked by mistake), then replaces
    <Path> with its artifacts. Run Register-AstelioTip.ps1 afterwards (as administrator) to install them.
.EXAMPLE
    ./tools/Get-AstelioTip.ps1; ./tools/Register-AstelioTip.ps1 -Path artifacts/tip
#>
[CmdletBinding()]
param(
    [string]$Path = (Join-Path $PSScriptRoot '..\artifacts\tip'),
    [string]$Repository = 'geekfujiwara/japanese-input'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$gh = Join-Path $env:LOCALAPPDATA 'Programs\gh\bin\gh.exe'
if (-not (Test-Path $gh)) {
    $gh = 'gh'
}
$arch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64') { 'arm64' } else { 'x64' }

$sha = & $gh api "repos/$Repository/commits/main" --jq .sha
if ($LASTEXITCODE -ne 0 -or -not $sha) {
    throw 'Could not read the latest commit on main (is gh logged in?).'
}
$run = & $gh run list --repo $Repository --workflow CI --commit $sha --limit 1 --json databaseId,status,conclusion | ConvertFrom-Json
if (-not $run) {
    throw "No CI run for main $($sha.Substring(0, 7)) yet; try again in a minute."
}
if ($run[0].status -ne 'completed') {
    Write-Host "Waiting for CI of main $($sha.Substring(0, 7)) to finish..."
    & $gh run watch $run[0].databaseId --repo $Repository --exit-status | Out-Null
    $run = & $gh run list --repo $Repository --workflow CI --commit $sha --limit 1 --json databaseId,status,conclusion | ConvertFrom-Json
}
if ($run[0].conclusion -ne 'success') {
    throw "CI for main $($sha.Substring(0, 7)) did not succeed ($($run[0].conclusion)); nothing downloaded."
}

# gh run download does not overwrite files, so start from an empty folder.
if (Test-Path $Path) {
    Remove-Item -LiteralPath $Path -Recurse -Force
}
& $gh run download $run[0].databaseId --repo $Repository -n "astelio-tip-windows-$arch" -n astelio-tip-windows-x86 -n astelio-dictionary -D $Path
if ($LASTEXITCODE -ne 0) {
    throw 'Download failed.'
}
# Register-AstelioTip.ps1 shows which build it installs.
Set-Content -LiteralPath (Join-Path $Path 'build.txt') -Value "main $sha" -Encoding utf8
Write-Host "Downloaded the build of main $($sha.Substring(0, 7)) to $Path"
