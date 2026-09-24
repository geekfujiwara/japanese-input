param(
    [ValidateSet('win-arm64', 'win-x64')]
    [string]$RuntimeIdentifier = 'win-arm64'
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path $PSScriptRoot -Parent
$projectPath = Join-Path $repositoryRoot 'src\KotohaIME.App\KotohaIME.App.csproj'
$outputDirectory = Join-Path $repositoryRoot "artifacts\AstelioIME-$RuntimeIdentifier"
$archivePath = Join-Path $repositoryRoot "artifacts\AstelioIME-$RuntimeIdentifier.zip"

if (Test-Path $outputDirectory) {
    Remove-Item $outputDirectory -Recurse -Force
}

if (Test-Path $archivePath) {
    Remove-Item $archivePath -Force
}

dotnet publish $projectPath -c Release -r $RuntimeIdentifier -o $outputDirectory
if ($LASTEXITCODE -ne 0) {
    throw "dotnet publish failed with exit code $LASTEXITCODE."
}

Compress-Archive -Path (Join-Path $outputDirectory '*') -DestinationPath $archivePath
Write-Output $archivePath