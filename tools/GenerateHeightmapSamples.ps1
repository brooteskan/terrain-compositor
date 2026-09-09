# Reproduce the height and issue-13 surface source data assets without an image editor or external packages.
# This writes only the named sample TIFFs/settings in the Gem's Assets/HeightmapStamps directory. It does not run Asset Processor.
# Unsigned 16-bit, single-channel, uncompressed, little-endian TIFF; rows start at image top.
$ErrorActionPreference = 'Stop'
$destination = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\integrations\o3de\Assets\HeightmapStamps'))
[IO.Directory]::CreateDirectory($destination) | Out-Null

function Write-R16Tiff([string]$name, [string]$pattern) {
    $size = 64
    $tags = @(
        @(256, 4, $size),      # ImageWidth (LONG)
        @(257, 4, $size),      # ImageLength
        @(258, 3, 16),         # BitsPerSample (SHORT)
        @(259, 3, 1),          # Compression: none
        @(262, 3, 1),          # Photometric: black is zero
        @(273, 4, 158),        # StripOffsets: immediately after the 12-entry IFD
        @(274, 3, 1),          # Orientation: top-left
        @(277, 3, 1),          # SamplesPerPixel
        @(278, 4, $size),      # RowsPerStrip
        @(279, 4, 8192),       # StripByteCounts: 64 * 64 * 2
        @(284, 3, 1),          # PlanarConfiguration: contiguous
        @(339, 3, 1)           # SampleFormat: unsigned integer
    )
    $stream = [IO.MemoryStream]::new()
    $writer = [IO.BinaryWriter]::new($stream)
    try {
        $writer.Write([byte[]]@(0x49, 0x49))
        $writer.Write([uint16]42)
        $writer.Write([uint32]8)
        $writer.Write([uint16]$tags.Count)
        foreach ($tag in $tags) {
            $writer.Write([uint16]$tag[0])
            $writer.Write([uint16]$tag[1])
            $writer.Write([uint32]1)
            # One SHORT also occupies this four-byte IFD value field (zero padding in the upper half).
            $writer.Write([uint32]$tag[2])
        }
        $writer.Write([uint32]0) # No additional IFDs/mips in the source TIFF.
        for ($y = 0; $y -lt $size; ++$y) {
            for ($x = 0; $x -lt $size; ++$x) {
                $value = 32768
                if ($pattern -eq 'landmarks') {
                    if ($y -lt 32) {
                        if ($x -lt 32) { $value = 65535 } else { $value = 49151 }
                    } else {
                        if ($x -lt 32) { $value = 16384 } else { $value = 0 }
                    }
                    # A mid-height L in the upper-left quadrant and a separate lower-right marker.
                    if (($x -ge 9 -and $x -le 12 -and $y -ge 9 -and $y -le 23) -or
                        ($x -ge 9 -and $x -le 23 -and $y -ge 20 -and $y -le 23)) { $value = 32768 }
                    if ($x -ge 44 -and $x -le 51 -and $y -ge 45 -and $y -le 52) { $value = 43690 }
                } elseif ($pattern -eq 'surface-a') {
                    if ($y -lt 32) {
                        if ($x -lt 32) { $value = 1 } else { $value = 2 }
                    } else {
                        if ($x -lt 32) { $value = 3 } else { $value = 0 }
                    }
                } elseif ($pattern -eq 'surface-b') {
                    if ($y -lt 32) { $value = 4 } else { $value = 2 }
                } elseif ($pattern -eq 'surface-blend') {
                    # Four exact vertical bands exercise b = 0, 0.25, 0.5, and 1.
                    if ($x -lt 16) { $value = 0 }
                    elseif ($x -lt 32) { $value = 16384 }
                    elseif ($x -lt 48) { $value = 32768 }
                    else { $value = 65535 }
                }
                $writer.Write([uint16]$value)
            }
        }
        $writer.Flush()
        [IO.File]::WriteAllBytes((Join-Path $destination $name), $stream.ToArray())
    } finally {
        $writer.Dispose()
        $stream.Dispose()
    }
}

# TextureSettings v2: EngineReduce serializes m_suppressEngineReduce, so true means Use Max Res.
# Unspecified fields use engine defaults. No per-platform overrides may reduce these sample sources.
$settings = @'
<ObjectStream version="3">
    <Class name="TextureSettings" version="2" type="{980132FF-C450-425D-8AE0-BD96A8486177}">
        <Class name="Name" field="Preset" value="GSI16" type="{3D2B920C-9EFD-40D5-AAE0-DF131C3D4931}"/>
        <Class name="unsigned int" field="SizeReduceLevel" value="0" type="{43DA906B-7DEF-4CA8-9790-854106D3F983}"/>
        <Class name="bool" field="EngineReduce" value="true" type="{A0CA880C-AFE4-43CB-926C-59AC48496112}"/>
        <Class name="bool" field="EnableMipmap" value="true" type="{A0CA880C-AFE4-43CB-926C-59AC48496112}"/>
    </Class>
</ObjectStream>
'@

Write-R16Tiff 'landmarks_gsi16.tiff' 'landmarks'
Write-R16Tiff 'constant_mid_gsi16.tiff' 'constant'
Write-R16Tiff 'surface_id_a_gsi16.tiff' 'surface-a'
Write-R16Tiff 'surface_id_b_gsi16.tiff' 'surface-b'
Write-R16Tiff 'surface_blend_gsi16.tiff' 'surface-blend'
foreach ($name in @(
    'landmarks_gsi16.tiff', 'constant_mid_gsi16.tiff',
    'surface_id_a_gsi16.tiff', 'surface_id_b_gsi16.tiff', 'surface_blend_gsi16.tiff')) {
    [IO.File]::WriteAllText((Join-Path $destination ($name + '.assetinfo')), $settings.Replace("`r`n", "`n") + "`n", [Text.UTF8Encoding]::new($false))
}
Get-ChildItem -LiteralPath $destination -File | Where-Object Name -in @(
    'landmarks_gsi16.tiff', 'constant_mid_gsi16.tiff',
    'surface_id_a_gsi16.tiff', 'surface_id_b_gsi16.tiff', 'surface_blend_gsi16.tiff',
    'landmarks_gsi16.tiff.assetinfo', 'constant_mid_gsi16.tiff.assetinfo',
    'surface_id_a_gsi16.tiff.assetinfo', 'surface_id_b_gsi16.tiff.assetinfo',
    'surface_blend_gsi16.tiff.assetinfo') | Select-Object Name, Length
