param(
    [switch]$SetupOnly
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$VenvPython = Join-Path $Root ".venv\Scripts\python.exe"
$DefaultPython = "C:\Python312\python.exe"

if (-not (Test-Path $VenvPython)) {
    if (Test-Path $DefaultPython) {
        & $DefaultPython -m venv (Join-Path $Root ".venv")
    } else {
        python -m venv (Join-Path $Root ".venv")
    }
}

& $VenvPython -m pip install --upgrade pip
& $VenvPython -m pip install -r (Join-Path $Root "requirements.txt")

if (-not $SetupOnly) {
    Push-Location $Root
    try {
        & $VenvPython -m uvicorn src.app:app --host 0.0.0.0 --port 8001
    } finally {
        Pop-Location
    }
}

