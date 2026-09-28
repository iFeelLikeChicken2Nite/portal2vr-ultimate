# Apply the Portal2VR startup guard to the pinned DXVK submodule before building.
# The patch lives in the superproject so a clean clone does not depend on an
# unpublished submodule commit. Run once after `git submodule update --init`.
$ErrorActionPreference = 'Stop'
$submodule = Join-Path $PSScriptRoot '..\dxvk'
$patch = Join-Path $PSScriptRoot 'dxvk-viewport-readiness.patch'

if (-not (Test-Path -LiteralPath (Join-Path $submodule 'src\d3d9\d3d9_device.cpp'))) {
    throw 'DXVK submodule is missing; run git submodule update --init first.'
}

$ErrorActionPreference = 'Continue'
& git -C $submodule apply --reverse --check $patch 2>$null
$alreadyApplied = $LASTEXITCODE -eq 0
$ErrorActionPreference = 'Stop'
if ($alreadyApplied) {
    Write-Output 'DXVK viewport guard is already applied.'
    exit 0
}

& git -C $submodule apply --check $patch
if ($LASTEXITCODE -ne 0) {
    throw 'DXVK patch does not apply cleanly; inspect the submodule before building.'
}

& git -C $submodule apply $patch
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to apply DXVK viewport guard.'
}
Write-Output 'Applied DXVK viewport guard.'
