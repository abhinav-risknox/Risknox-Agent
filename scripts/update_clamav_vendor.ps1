param(
    [string]$Version = "latest",
    [string]$Repo = "Cisco-Talos/clamav",
    [string]$VendorDir = "vendor/clamav",
    [switch]$UpdateDatabases,
    [switch]$SkipSmokeTest
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path (Join-Path $PSScriptRoot "..") $Path))
}

function Get-Json([string]$Uri) {
    Invoke-RestMethod -Uri $Uri -Headers @{
        "User-Agent" = "Risknox-ClamAV-Vendor-Updater"
        "Accept" = "application/vnd.github+json"
    }
}

function Copy-DirectoryContents([string]$Source, [string]$Destination) {
    if (!(Test-Path $Destination)) {
        New-Item -ItemType Directory -Path $Destination | Out-Null
    }

    Get-ChildItem -Path $Source -Force | ForEach-Object {
        $target = Join-Path $Destination $_.Name
        Copy-Item -LiteralPath $_.FullName -Destination $target -Recurse -Force
    }
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$vendorPath = Resolve-RepoPath $VendorDir
$vendorParent = Split-Path -Parent $vendorPath
$resolvedVendorParent = [System.IO.Path]::GetFullPath($vendorParent)

if (!$vendorPath.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to update vendor directory outside repository: $vendorPath"
}

$releaseUri = if ($Version -eq "latest") {
    "https://api.github.com/repos/$Repo/releases/latest"
} else {
    "https://api.github.com/repos/$Repo/releases/tags/clamav-$Version"
}

Write-Host "Fetching ClamAV release metadata from $releaseUri"
$release = Get-Json $releaseUri

$portableZip = $release.assets |
    Where-Object {
        $_.name -match '\.zip$' -and
        $_.name -notmatch '\.sig$' -and
        (
            $_.name -match 'win.*x64.*portable' -or
            $_.name -match 'windows.*x64.*portable' -or
            $_.name -match 'win.*x86_64.*portable' -or
            $_.name -match 'clamav-.*win.*x64'
        )
    } |
    Sort-Object name |
    Select-Object -First 1

if (!$portableZip) {
    $assetNames = ($release.assets | ForEach-Object { $_.name }) -join "`n  "
    throw "Could not find a Windows x64 portable ZIP in release $($release.tag_name). Assets:`n  $assetNames"
}

$tmpRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("risknox-clamav-" + [System.Guid]::NewGuid().ToString("N"))
$downloadPath = Join-Path $tmpRoot $portableZip.name
$extractPath = Join-Path $tmpRoot "extract"

New-Item -ItemType Directory -Path $tmpRoot, $extractPath | Out-Null

try {
    Write-Host "Downloading $($portableZip.name)"
    Invoke-WebRequest -Uri $portableZip.browser_download_url -OutFile $downloadPath -Headers @{
        "User-Agent" = "Risknox-ClamAV-Vendor-Updater"
    }

    $sha256 = (Get-FileHash -Algorithm SHA256 -Path $downloadPath).Hash.ToLowerInvariant()
    Write-Host "SHA256 $sha256"

    Expand-Archive -Path $downloadPath -DestinationPath $extractPath -Force

    $clamRoot = Get-ChildItem -Path $extractPath -Recurse -Filter clamscan.exe -File |
        Select-Object -First 1 |
        ForEach-Object { $_.Directory.FullName }

    if (!$clamRoot) {
        throw "Downloaded ZIP did not contain clamscan.exe"
    }

    $preservedDb = Join-Path $tmpRoot "database"
    $existingDb = Join-Path $vendorPath "database"
    if ((Test-Path $existingDb) -and !$UpdateDatabases) {
        Copy-Item -LiteralPath $existingDb -Destination $preservedDb -Recurse -Force
    }

    $installerFreshclam = Join-Path $repoRoot "installer_assets/freshclam.conf"
    $installerUpdater = Join-Path $repoRoot "installer_assets/update_definitions.bat"

    if (!(Test-Path $resolvedVendorParent)) {
        New-Item -ItemType Directory -Path $resolvedVendorParent | Out-Null
    }

    if (Test-Path $vendorPath) {
        $resolvedVendor = [System.IO.Path]::GetFullPath((Resolve-Path $vendorPath).Path)
        if (!$resolvedVendor.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to clean vendor directory outside repository: $resolvedVendor"
        }
        Remove-Item -LiteralPath $vendorPath -Recurse -Force
    }

    New-Item -ItemType Directory -Path $vendorPath | Out-Null
    Copy-DirectoryContents -Source $clamRoot -Destination $vendorPath

    Get-ChildItem -Path $vendorPath -Recurse -Include *.pdb,*.lib -File | Remove-Item -Force
    foreach ($extraDir in @("include", "UserManual", "conf_examples", "COPYING")) {
        $path = Join-Path $vendorPath $extraDir
        if (Test-Path $path) {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
    }

    if ((Test-Path $preservedDb) -and !(Test-Path (Join-Path $vendorPath "database"))) {
        Copy-Item -LiteralPath $preservedDb -Destination (Join-Path $vendorPath "database") -Recurse -Force
    }

    Copy-Item -LiteralPath $installerFreshclam -Destination (Join-Path $vendorPath "freshclam.conf") -Force
    Copy-Item -LiteralPath $installerUpdater -Destination (Join-Path $vendorPath "update_definitions.bat") -Force

    $certDir = Join-Path $vendorPath "certs"
    $certPath = Join-Path $certDir "clamav.crt"
    if (!(Test-Path $certPath)) {
        New-Item -ItemType Directory -Path $certDir -Force | Out-Null
        $tag = $release.tag_name
        $certUri = "https://raw.githubusercontent.com/$Repo/$tag/certs/clamav.crt"
        Write-Host "Downloading CVD verification certificate from $certUri"
        Invoke-WebRequest -Uri $certUri -OutFile $certPath -Headers @{
            "User-Agent" = "Risknox-ClamAV-Vendor-Updater"
        }
    }

    if ($UpdateDatabases) {
        $dbDir = Join-Path $vendorPath "database"
        New-Item -ItemType Directory -Path $dbDir -Force | Out-Null
        $freshclamConf = Join-Path $vendorPath "freshclam.conf"
        & (Join-Path $vendorPath "freshclam.exe") `
            "--config-file=$freshclamConf" `
            --datadir=$dbDir `
            --cvdcertsdir=$certDir
        if ($LASTEXITCODE -ne 0) {
            throw "freshclam failed with exit code $LASTEXITCODE"
        }
    }

    $manifest = [ordered]@{
        version = ($release.tag_name -replace '^clamav-', '')
        tag = $release.tag_name
        source = $release.html_url
        assetName = $portableZip.name
        assetUrl = $portableZip.browser_download_url
        assetSha256 = $sha256
        updatedAtUtc = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
        databasePolicy = if ($UpdateDatabases) { "updated-by-freshclam" } else { "preserved-existing-or-release" }
    }

    $manifest | ConvertTo-Json -Depth 5 | Set-Content -Path (Join-Path $vendorPath "manifest.json") -Encoding UTF8

    if (!$SkipSmokeTest) {
        & (Join-Path $vendorPath "clamscan.exe") --version
        if ($LASTEXITCODE -ne 0) {
            throw "clamscan --version failed with exit code $LASTEXITCODE"
        }

        $freshclamConf = Join-Path $vendorPath "freshclam.conf"
        & (Join-Path $vendorPath "freshclam.exe") "--config-file=$freshclamConf" --version
        if ($LASTEXITCODE -ne 0) {
            throw "freshclam --version failed with exit code $LASTEXITCODE"
        }

        $dbDir = Join-Path $vendorPath "database"
        if (Test-Path $dbDir) {
            & (Join-Path $vendorPath "clamscan.exe") `
                --database=$dbDir `
                --cvdcertsdir=$certDir `
                --no-summary `
                (Join-Path $repoRoot "installer.iss")
            if ($LASTEXITCODE -ne 0) {
                throw "clamscan smoke test failed with exit code $LASTEXITCODE"
            }
        } else {
            Write-Warning "No vendor database directory found; skipping scan smoke test."
        }
    }

    Write-Host "ClamAV vendor bundle updated to $($release.tag_name) at $vendorPath"
} finally {
    if (Test-Path $tmpRoot) {
        Remove-Item -LiteralPath $tmpRoot -Recurse -Force
    }
}
