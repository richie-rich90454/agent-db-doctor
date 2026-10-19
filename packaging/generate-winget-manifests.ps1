# Fills the winget manifest templates from a release zip.
#
# The installer hash and the download URL can only exist once a release exists,
# so the three manifests ship as templates and this script writes the finished
# files beside them. Run it after downloading the release zip, then validate and
# open the pull request as packaging/README.md describes.
#
# Example:
#   .\packaging\generate-winget-manifests.ps1 -Version 1.0.0 `
#       -ZipPath .\agent-db-doctor-1.0.0-windows-x86_64.zip
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [Parameter(Mandatory = $true)]
    [string]$ZipPath,
    [string]$OutputDirectory = ""
)
$ErrorActionPreference = "Stop"

if (-not $Version -match '^[0-9]+\.[0-9]+\.[0-9]+$') {
    throw "The version must look like 1.0.0; got '$Version'."
}
if (-not (Test-Path -Path $ZipPath -PathType Leaf)) {
    throw "The release zip was not found at '$ZipPath'."
}
if ([string]::IsNullOrEmpty($OutputDirectory)) {
    $OutputDirectory = Join-Path $PSScriptRoot "winget"
}
if (-not (Test-Path -Path $OutputDirectory -PathType Container)) {
    throw "The output directory was not found at '$OutputDirectory'."
}

# The URL names the asset the continuous integration workflow attaches to the
# release; keep the two in step if the package naming ever changes.
$zip = Get-Item -Path $ZipPath
$installerSha256 = (Get-FileHash -Path $zip.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
$installerUrl = "https://github.com/richie-rich90454/agent-db-doctor/releases/download/v$Version/agent-db-doctor-$Version-windows-x86_64.zip"

$replacements = @{
    "{{VERSION}}" = $Version
    "{{URL}}" = $installerUrl
    "{{SHA256}}" = $installerSha256
}

# Template to finished manifest. The finished names carry the full package
# identifier because that is the naming the community repository expects.
$manifests = @(
    @{ Template = "agent-db-doctor.yaml.template"
       Output = "richie-rich90454.agent-db-doctor.yaml" }
    @{ Template = "agent-db-doctor.locale.en-US.yaml.template"
       Output = "richie-rich90454.agent-db-doctor.locale.en-US.yaml" }
    @{ Template = "agent-db-doctor.installer.yaml.template"
       Output = "richie-rich90454.agent-db-doctor.installer.yaml" }
)

# The files are written without a byte order mark, because the YAML readers on
# the validation path treat a mark as the first character of the document.
$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
foreach ($manifest in $manifests) {
    $templatePath = Join-Path (Join-Path $PSScriptRoot "winget") $manifest.Template
    if (-not (Test-Path -Path $templatePath -PathType Leaf)) {
        throw "The template was not found at '$templatePath'."
    }
    $text = Get-Content -Path $templatePath -Raw
    foreach ($placeholder in $replacements.Keys) {
        $text = $text.Replace($placeholder, $replacements[$placeholder])
    }
    # A leftover placeholder means template and script have drifted apart; a
    # manifest with a literal {{...}} in it would fail validation at the other
    # end of the pull request instead, which is the more expensive place to find out.
    if ($text.Contains("{{")) {
        throw "The template $($manifest.Template) still holds a placeholder this script does not fill."
    }
    $outputPath = Join-Path $OutputDirectory $manifest.Output
    [System.IO.File]::WriteAllText($outputPath, $text, $utf8WithoutBom)
    Write-Host "Wrote $outputPath"
}
Write-Host "Validate the three files with: wingetcreate validate `"$OutputDirectory`""
