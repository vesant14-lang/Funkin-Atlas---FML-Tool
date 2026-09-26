param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

Add-Type -AssemblyName System.Drawing
$source = [System.Drawing.Bitmap]::FromFile([System.IO.Path]::GetFullPath($InputPath))
try {
    $sizes = @(16, 32, 48, 256)
    $images = @()
    foreach ($size in $sizes) {
        $bitmap = New-Object System.Drawing.Bitmap($size, $size)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear([System.Drawing.Color]::Black)
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
            $fit = [Math]::Min($size / $source.Width, $size / $source.Height)
            $width = [int][Math]::Round($source.Width * $fit)
            $height = [int][Math]::Round($source.Height * $fit)
            $graphics.DrawImage($source, [int](($size - $width) / 2), [int](($size - $height) / 2), $width, $height)
            $memory = New-Object System.IO.MemoryStream
            $bitmap.Save($memory, [System.Drawing.Imaging.ImageFormat]::Png)
            $images += ,$memory.ToArray()
            $memory.Dispose()
        } finally {
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
    $destination = [System.IO.Path]::GetFullPath($OutputPath)
    [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($destination)) | Out-Null
    $stream = [System.IO.File]::Create($destination)
    try {
        $writer = New-Object System.IO.BinaryWriter($stream)
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]$sizes.Count)
        $offset = 6 + 16 * $sizes.Count
        for ($index = 0; $index -lt $sizes.Count; $index++) {
            $size = $sizes[$index]
            $writer.Write([byte]($size % 256))
            $writer.Write([byte]($size % 256))
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]$images[$index].Length)
            $writer.Write([uint32]$offset)
            $offset += $images[$index].Length
        }
        foreach ($bytes in $images) { $writer.Write([byte[]]$bytes) }
        $writer.Flush()
    } finally {
        $stream.Dispose()
    }
} finally {
    $source.Dispose()
}
