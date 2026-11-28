$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('openMenu logo [test] ' + [Guid]::NewGuid())
$ipRelative = 'src\GDMENUCardManager.Core\tools\openMenu\IP.BIN'
$fontRelative = 'src\GDMENUCardManager.Core\tools\openMenu\menu_data\font\GDMNUFNT.pvr'
$helperRelative = 'build-assets\openmenu-logo\Update-OpenMenuLogo.ps1'
$templateRelative = 'build-assets\openmenu-logo\base.png'

function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Get-Hash([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return [BitConverter]::ToString($sha.ComputeHash($Bytes)).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
}

function Read-Logo([byte[]]$Ip) {
    $start = 0x3820
    Assert ($Ip[$start] -eq 0x4d -and $Ip[$start + 1] -eq 0x52) 'MR signature is missing.'
    $size = [BitConverter]::ToUInt32($Ip, $start + 2)
    $offset = [BitConverter]::ToUInt32($Ip, $start + 10)
    $width = [BitConverter]::ToUInt32($Ip, $start + 14)
    $height = [BitConverter]::ToUInt32($Ip, $start + 18)
    $colors = [BitConverter]::ToUInt32($Ip, $start + 26)
    Assert ($size -le 8192 -and $offset -lt $size) 'MR size is invalid.'
    Assert ($width -eq 320 -and $height -eq 90) 'MR dimensions changed.'
    Assert ($colors -gt 0 -and $colors -le 128 -and $offset -eq 30 + 4 * $colors) 'MR palette is invalid.'
    $rgb = New-Object byte[] (320 * 90 * 3)
    $position = $start + $offset
    $written = 0
    while ($position -lt $start + $size) {
        $marker = $Ip[$position++]
        if ($marker -lt 128) {
            $run = 1
            $color = $marker
        } elseif ($marker -eq 129) {
            $run = $Ip[$position++]
            $color = $Ip[$position++]
        } elseif ($marker -eq 130 -and $Ip[$position] -ge 128) {
            $run = 128 + $Ip[$position++]
            $color = $Ip[$position++]
        } else {
            $run = $marker - 128
            $color = $Ip[$position++]
        }
        Assert ($run -gt 0 -and $color -lt $colors -and $written + $run * 3 -le $rgb.Length) 'MR run is invalid.'
        $palette = $start + 30 + $color * 4
        for ($i = 0; $i -lt $run; $i++) {
            $rgb[$written++] = $Ip[$palette + 2]
            $rgb[$written++] = $Ip[$palette + 1]
            $rgb[$written++] = $Ip[$palette]
        }
    }
    Assert ($written -eq $rgb.Length -and $position -eq $start + $size) 'MR pixel count is invalid.'
    return @{ Size = $size; Pixels = $rgb }
}

function Assert-Rejected([string]$Version) {
    $before = Get-Hash ([IO.File]::ReadAllBytes($ipPath))
    $rejected = $false
    try { & $helper -Version $Version } catch { $rejected = $true }
    Assert $rejected 'Invalid input was accepted.'
    Assert ((Get-Hash ([IO.File]::ReadAllBytes($ipPath))) -eq $before) 'Rejected input changed IP.BIN.'
}

try {
    foreach ($relative in @($helperRelative, $templateRelative, $ipRelative, $fontRelative)) {
        $source = Join-Path $root $relative
        Assert ([IO.File]::Exists($source)) ('Required file is missing: ' + $relative)
        $destination = Join-Path $temporary $relative
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
        [IO.File]::Copy($source, $destination)
    }
    $helper = Join-Path $temporary $helperRelative
    $ipPath = Join-Path $temporary $ipRelative
    $fontPath = Join-Path $temporary $fontRelative
    $templatePath = Join-Path $temporary $templateRelative
    $original = [IO.File]::ReadAllBytes($ipPath)

    # These RGB hashes come from the reviewed mockups.
    $expected = [ordered]@{
        'v1.0.0-ateam' = '8a2852fc6fe693d0a8dcc2744c3f117654e3f52372529a5ac3e6a8bfb357fae5'
        'v1.6.5-ateam' = '3a0c007ca8133d991ae0d09f4911927e5796880a18176d38fdedad55c06ffe5d'
        'v1.7.0-ateam' = 'c025a021c1b55b58b6297d19659d2464e6a2ca9cf2a480e26b896be1df7c150b'
        'v2.0.0-ateam' = '17f931f76b48586065084802b6840741c2b352c492a2c8c2e0a9b20e218f9d99'
        'v1.7.2-blaskkaffe' = 'a9c9093dec9aef464dceefee8cbc18ae0ba8b4b663e4b5dd61c5e6669ae219ac'
    }
    foreach ($version in $expected.Keys) {
        $before = [IO.File]::ReadAllBytes($ipPath)
        & $helper -Version $version
        $after = [IO.File]::ReadAllBytes($ipPath)
        Assert ($after.Length -eq $before.Length) 'IP.BIN size changed.'
        $logo = Read-Logo $after
        Assert ((Get-Hash $logo.Pixels) -eq $expected[$version]) ('Logo pixels differ from the mockup for ' + $version)
        for ($i = 0; $i -lt $before.Length; $i++) {
            if ($i -lt 0x3820 -or $i -ge 0x3820 + $logo.Size) {
                Assert ($before[$i] -eq $after[$i]) 'Bytes outside the logo changed.'
            }
        }
        $stamp = [DateTime]::SpecifyKind([DateTime]'2000-01-01', [DateTimeKind]::Utc)
        [IO.File]::SetLastWriteTimeUtc($ipPath, $stamp)
        & $helper -Version $version
        Assert ((Get-Hash ([IO.File]::ReadAllBytes($ipPath))) -eq (Get-Hash $after)) 'Repeated generation changed the image.'
        Assert ([IO.File]::GetLastWriteTimeUtc($ipPath) -eq $stamp) 'Repeated generation rewrote IP.BIN.'
    }

    Assert-Rejected 'v1234567890.1234567890.1234567890-ateam'
    Assert-Rejected ('v1.7.0-' + [char]0x2603)
    Assert-Rejected ''

    [IO.File]::WriteAllBytes($ipPath, (New-Object byte[] 32768))
    Assert-Rejected 'v1.7.0-ateam'
    [IO.File]::WriteAllBytes($ipPath, $original[0..100])
    Assert-Rejected 'v1.7.0-ateam'
    [IO.File]::WriteAllBytes($ipPath, $original)

    $font = [IO.File]::ReadAllBytes($fontPath)
    $badFont = [byte[]]$font.Clone()
    $badFont[24] = 1
    [IO.File]::WriteAllBytes($fontPath, $badFont)
    Assert-Rejected 'v1.7.0-ateam'
    [IO.File]::WriteAllBytes($fontPath, $font)

    $small = [Drawing.Bitmap]::new(1, 1)
    try { $small.Save($templatePath, [Drawing.Imaging.ImageFormat]::Png) } finally { $small.Dispose() }
    Assert-Rejected 'v1.7.0-ateam'
    Assert ((Get-Hash ([IO.File]::ReadAllBytes((Join-Path $root $ipRelative)))) -eq (Get-Hash $original)) 'Tests changed the source IP.BIN.'
    Write-Host 'Passed: five mockups, byte preservation, repeated builds, invalid inputs, and paths with spaces and brackets.'
} finally {
    if ([IO.Directory]::Exists($temporary)) { [IO.Directory]::Delete($temporary, $true) }
}
