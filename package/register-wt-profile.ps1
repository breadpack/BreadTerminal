# Register BreadTerminal as a Windows Terminal profile.
# Called by the MSI installer as a post-install custom action.
param(
    [string]$InstallDir
)

if (-not $InstallDir) {
    $InstallDir = $PSScriptRoot
}
# Trim trailing backslash to avoid path issues
$InstallDir = $InstallDir.TrimEnd('\')

$exePath = Join-Path $InstallDir "BreadTerminal.exe"
if (-not (Test-Path $exePath)) { exit 0 }

# Windows Terminal settings paths (Store + unpackaged)
$settingsPaths = @(
    "$env:LOCALAPPDATA\Packages\Microsoft.WindowsTerminal_8wekyb3d8bbwe\LocalState\settings.json",
    "$env:LOCALAPPDATA\Microsoft\Windows Terminal\settings.json"
)

$profileGuid = "{e8a1b2c3-d4e5-6f7a-8b9c-0d1e2f3a4b5d}"

foreach ($path in $settingsPaths) {
    if (-not (Test-Path $path)) { continue }

    try {
        $json = Get-Content $path -Raw -Encoding UTF8 | ConvertFrom-Json

        if (-not $json.profiles) { continue }
        if (-not $json.profiles.list) { continue }

        # Check if already registered
        $existing = $json.profiles.list | Where-Object { $_.guid -eq $profileGuid }
        if ($existing) { continue }

        # Add BreadTerminal profile
        $newProfile = [PSCustomObject]@{
            guid        = $profileGuid
            name        = "BreadTerminal"
            commandline = $exePath
            icon        = $exePath
            hidden      = $false
        }

        $json.profiles.list += $newProfile

        $json | ConvertTo-Json -Depth 32 | Set-Content $path -Encoding UTF8
    } catch {
        # Silently continue if settings.json parsing fails
        continue
    }
}
