[CmdletBinding()]
param(
    [string]$InputExecutable,
    [string]$OutputDirectory,
    [ValidatePattern('^[0-9A-Za-z][0-9A-Za-z._-]*$')]
    [string]$Version = 'local'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $InputExecutable) { $InputExecutable = Join-Path $projectRoot 'bin/MacroPillControl.exe' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'dist' }
$executable = Get-Item -LiteralPath $InputExecutable
if ($executable.PSIsContainer -or $executable.Length -eq 0) {
    throw 'Informe o executável compilado, com conteúdo.'
}

$packageName = "MacroPillControl-windows-x64-$Version"
$stage = Join-Path $OutputDirectory $packageName
$archivePath = Join-Path $OutputDirectory "$packageName.zip"
$archiveChecksums = Join-Path $OutputDirectory 'SHA256SUMS.txt'
foreach ($target in @($stage, $archivePath, $archiveChecksums)) {
    if (Test-Path -LiteralPath $target) {
        throw "A saída já existe: $target. Use uma pasta de saída nova."
    }
}

New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -LiteralPath $executable.FullName -Destination (Join-Path $stage 'MacroPillControl.exe')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README-distribuicao.txt') -Destination (Join-Path $stage 'LEIA-ME.txt')
$fileChecksums = foreach ($name in @('MacroPillControl.exe', 'LEIA-ME.txt')) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $stage $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    '{0}  {1}' -f $hash, $name
}
$fileChecksums | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding utf8
Compress-Archive -LiteralPath $stage -DestinationPath $archivePath -CompressionLevel Optimal
$zipHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
'{0}  {1}' -f $zipHash, (Split-Path -Leaf $archivePath) | Set-Content -LiteralPath $archiveChecksums -Encoding utf8
Write-Output "Pacote: $archivePath"
Write-Output "SHA256: $zipHash"
