param(
    [string]$OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'FunkinAtlas-delivery')
)

$sourceRoot = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$destination = [System.IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $destination) {
    throw "The output directory already exists: $destination"
}
foreach ($name in @('FunkinAtlas.exe', 'SDL3.dll', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'docs/PUBLIC_README.md')) {
    if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot $name) -PathType Leaf)) {
        throw "Missing release input: $name"
    }
}
$changes = & git -C $sourceRoot status --porcelain
if ($LASTEXITCODE -ne 0 -or $changes) {
    throw 'Commit source changes before packaging the developer archive.'
}
$developer = Join-Path $destination 'Developer'
$public = Join-Path $destination 'Public'
New-Item -ItemType Directory -Path $developer, $public -Force | Out-Null
$sourceZip = Join-Path $destination 'FunkinAtlas-source.zip'
& git -C $sourceRoot archive --format=zip --output=$sourceZip HEAD
if ($LASTEXITCODE -ne 0) {
    throw 'Could not create the source archive.'
}
Expand-Archive -LiteralPath $sourceZip -DestinationPath $developer
Copy-Item -LiteralPath (Join-Path $sourceRoot 'FunkinAtlas.exe') -Destination $public
Copy-Item -LiteralPath (Join-Path $sourceRoot 'SDL3.dll') -Destination $public
Copy-Item -LiteralPath (Join-Path $sourceRoot 'LICENSE') -Destination $public
Copy-Item -LiteralPath (Join-Path $sourceRoot 'THIRD_PARTY_NOTICES.md') -Destination $public
Copy-Item -LiteralPath (Join-Path $sourceRoot 'docs/PUBLIC_README.md') -Destination (Join-Path $public 'README.md')
Compress-Archive -Path (Join-Path $public '*') -DestinationPath (Join-Path $destination 'FunkinAtlas-public.zip')
Write-Output $destination
