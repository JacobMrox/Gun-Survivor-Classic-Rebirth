param(
    [string]$Directory = (Join-Path $PSScriptRoot '..\data')
)

$ErrorActionPreference = 'Stop'
$required = @(
    'avcodec-61.dll',
    'avformat-61.dll',
    'avutil-59.dll',
    'swscale-8.dll',
    'swresample-5.dll'
)

foreach ($name in $required) {
    $path = Join-Path $Directory $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing FFmpeg runtime: $name"
    }
    $image = [IO.File]::ReadAllBytes($path)
    if ($image.Length -lt 64 -or $image[0] -ne 0x4D -or $image[1] -ne 0x5A) {
        throw "Invalid PE image: $name"
    }
    $header = [BitConverter]::ToInt32($image, 0x3C)
    if ($header -lt 64 -or $header -gt ($image.Length - 24) -or
        [BitConverter]::ToUInt32($image, $header) -ne 0x00004550 -or
        [BitConverter]::ToUInt16($image, $header + 4) -ne 0x014C) {
        throw "FFmpeg runtime must be an x86 PE image: $name"
    }
}

foreach ($name in @('FFmpeg-LGPL-2.1.txt', 'FFmpeg-NOTICE.txt')) {
    if (-not (Test-Path -LiteralPath (Join-Path $Directory "licenses\$name") -PathType Leaf)) {
        throw "Missing FFmpeg notice: $name"
    }
}

Write-Output 'FFmpeg runtime check passed: all five x86 DLLs and notices are present.'
