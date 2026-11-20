param(
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$DestDir
)
New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
$null = robocopy $SourceDir $DestDir /E /XO /MT:16 /NFL /NDL /NJH /NJS /NC /NS /NP
if ($LASTEXITCODE -lt 8) { exit 0 }
exit $LASTEXITCODE
