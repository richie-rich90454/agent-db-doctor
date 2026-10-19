# Fills the Homebrew formula template from the two release tarballs.
#
# The two hashes can only exist once a release exists, so the formula ships as a
# template and this script writes the finished formula beside it. Run it after
# downloading the macOS arm64 tarball and the Linux x86_64 tarball, then commit
# the result to the tap as packaging/README.md describes.
#
# Example:
#   .\packaging\generate-homebrew-formula.ps1 -Version 1.0.0 `
#       -MacosTarGz .\agent-db-doctor-1.0.0-macos-arm64.tar.gz `
#       -LinuxTarGz .\agent-db-doctor-1.0.0-linux-x86_64-gcc.tar.gz
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [Parameter(Mandatory = $true)]
    [string]$MacosTarGz,
    [Parameter(Mandatory = $true)]
    [string]$LinuxTarGz,
    [string]$OutputPath = ""
)
$ErrorActionPreference = "Stop"

if (-not $Version -match '^[0-9]+\.[0-9]+\.[0-9]+$') {
    throw "The version must look like 1.0.0; got '$Version'."
}
foreach ($tarball in @($MacosTarGz, $LinuxTarGz)) {
    if (-not (Test-Path -Path $tarball -PathType Leaf)) {
        throw "The release tarball was not found at '$tarball'."
    }
}
# The two tarballs fill different hashes, and swapping them would produce a formula
# that fails for every user of one platform; the names are checked so a slip at
# the command line cannot do that silently.
$macos = Get-Item -Path $MacosTarGz
$linux = Get-Item -Path $LinuxTarGz
if ($macos.Name -notmatch 'macos-arm64') {
    throw "The macOS tarball name should contain 'macos-arm64'; got '$($macos.Name)'."
}
if ($linux.Name -notmatch 'linux-x86_64') {
    throw "The Linux tarball name should contain 'linux-x86_64'; got '$($linux.Name)'."
}

$macosSha256 = (Get-FileHash -Path $macos.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
$linuxSha256 = (Get-FileHash -Path $linux.FullName -Algorithm SHA256).Hash.ToLowerInvariant()

if ([string]::IsNullOrEmpty($OutputPath)) {
    $OutputPath = Join-Path (Join-Path $PSScriptRoot "homebrew") "agent-db-doctor.rb"
}
$templatePath = Join-Path (Join-Path $PSScriptRoot "homebrew") "agent-db-doctor.rb.template"
if (-not (Test-Path -Path $templatePath -PathType Leaf)) {
    throw "The template was not found at '$templatePath'."
}

$replacements = @{
    "{{VERSION}}" = $Version
    "{{MACOS_SHA256}}" = $macosSha256
    "{{LINUX_SHA256}}" = $linuxSha256
}
$text = Get-Content -Path $templatePath -Raw
foreach ($placeholder in $replacements.Keys) {
    $text = $text.Replace($placeholder, $replacements[$placeholder])
}
# A leftover placeholder means template and script have drifted apart; better to
# stop here than to publish a formula whose hash is the literal {{...}}.
if ($text.Contains("{{")) {
    throw "The template still holds a placeholder this script does not fill."
}

# The formula is written without a byte order mark, because the Ruby parser would
# treat a mark as part of the first line.
$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($OutputPath, $text, $utf8WithoutBom)
Write-Host "Wrote $OutputPath"
