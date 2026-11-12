# Builds ExBox-Setup.exe with Windows Python (copied from Omnibloom Setup's build-win.ps1, adapted).
# Uses its own Python environment (dashboard/.venv-setup) and build folders (dashboard/.build-setup,
# dashboard/dist). Runs Setup's tests first, then checks the built app. Never contacts the PS5.
#     powershell -ExecutionPolicy Bypass -File dashboard\build-setup-exe.ps1
$ErrorActionPreference = 'Stop'
$dash = $PSScriptRoot
$root = Split-Path -Parent $dash
Set-Location -LiteralPath $root

$venv = Join-Path $dash '.venv-setup'
$py = Join-Path $venv 'Scripts\python.exe'
if (-not (Test-Path -LiteralPath $py)) {
  if (Get-Command py -ErrorAction SilentlyContinue) { & py -3 -m venv $venv }
  else {
    $base = Get-Command python -ErrorAction SilentlyContinue | Where-Object { $_.Source -notlike '*WindowsApps*' } | Select-Object -First 1
    if (-not $base) { Write-Host 'No usable Windows Python (install it from python.org).'; exit 3 }
    & $base.Source -m venv $venv
  }
}
& $py -m pip install --disable-pip-version-check -q 'pyinstaller==6.16.0' 'pillow==12.0.0'
if ($LASTEXITCODE) { exit $LASTEXITCODE }

Write-Host 'Tests:'
& $py -B -m unittest discover -s dashboard\tests -p 'test_installer.py'
if ($LASTEXITCODE) { Write-Host 'Tests failed; nothing was built.'; exit 1 }

Write-Host 'Build:'
& $py -m PyInstaller --noconfirm --log-level WARN --distpath (Join-Path $dash 'dist') --workpath (Join-Path $dash '.build-setup') (Join-Path $dash 'exbox-setup.spec')
$exe = Join-Path $dash 'dist\ExBox-Setup.exe'
if (-not (Test-Path -LiteralPath $exe)) { Write-Host 'Build failed.'; exit 1 }

Write-Host 'Checking the built app:'
& $py -B (Join-Path $dash 'tests\frozen_check.py') $exe
if ($LASTEXITCODE) { Write-Host 'The built app did not pass its check.'; exit 1 }
$item = Get-Item -LiteralPath $exe
Write-Host ("Built {0} ({1:N1} MB) sha256 {2}" -f $item.FullName, ($item.Length / 1MB), (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLower())
