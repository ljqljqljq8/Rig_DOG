param(
    [switch]$SetupOnly
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$VenvPython = Join-Path $Root ".venv\Scripts\python.exe"

function Resolve-BasePython {
    $candidates = @(
        "C:\Python312\python.exe",
        (Join-Path $env:LocalAppData "Programs\Python\Python312\python.exe"),
        (Join-Path $env:LocalAppData "Programs\Python\Python311\python.exe"),
        "C:\Python311\python.exe"
    )

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) {
            return $candidate
        }
    }

    $pythonCmd = Get-Command python -ErrorAction SilentlyContinue
    if ($pythonCmd) {
        return $pythonCmd.Source
    }

    throw "No suitable Python interpreter found. Install Python 3.12 or 3.11 first."
}

if (-not (Test-Path $VenvPython)) {
    $BasePython = Resolve-BasePython
    & $BasePython -m venv (Join-Path $Root ".venv")
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

