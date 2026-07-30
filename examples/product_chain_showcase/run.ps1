param(
    [Parameter(Mandatory = $true)]
    [string]$BinDir,

    [Parameter(Mandatory = $true)]
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

$scriptRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot "..\.."))
$binaryRoot = [System.IO.Path]::GetFullPath($BinDir)
$outputRoot = [System.IO.Path]::GetFullPath($OutputDir)
$filesystemRoot = [System.IO.Path]::GetPathRoot($outputRoot)

foreach ($forbidden in @($repositoryRoot, $binaryRoot, $filesystemRoot)) {
    if ([string]::Equals($outputRoot.TrimEnd('\', '/'), $forbidden.TrimEnd('\', '/'),
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing destructive showcase output path: $outputRoot"
    }
}

$outputMarker = Join-Path $outputRoot ".toolx-showcase-output"
if (Test-Path -LiteralPath $outputRoot) {
    if (-not (Test-Path -LiteralPath $outputMarker -PathType Leaf)) {
        throw "Refusing to clean an existing directory without a ToolX showcase marker: $outputRoot"
    }
    Remove-Item -LiteralPath $outputRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $outputRoot | Out-Null
Set-Content -LiteralPath $outputMarker -Value "ToolX product-chain showcase output" -Encoding utf8

$fixtureRoot = Join-Path $scriptRoot "fixtures"
$inputRoot = Join-Path $outputRoot "input"
Copy-Item -LiteralPath $fixtureRoot -Destination $inputRoot -Recurse

function Resolve-ShowcaseBinary([string]$name) {
    $candidate = Join-Path $binaryRoot "$name.exe"
    if (-not (Test-Path -LiteralPath $candidate)) {
        $candidate = Join-Path $binaryRoot $name
    }
    if (-not (Test-Path -LiteralPath $candidate)) {
        throw "Required showcase binary is missing: $name under $binaryRoot"
    }
    return [System.IO.Path]::GetFullPath($candidate)
}

function Invoke-ShowcaseTool(
    [string]$name,
    [string[]]$arguments,
    [string]$captureName
) {
    $executable = Resolve-ShowcaseBinary $name
    $stdoutPath = Join-Path $outputRoot $captureName
    $stderrPath = "$stdoutPath.stderr"
    & $executable @arguments 1> $stdoutPath 2> $stderrPath
    if ($LASTEXITCODE -ne 0) {
        throw "$name failed with exit $LASTEXITCODE. See $stderrPath"
    }
}

$merged = Join-Path $outputRoot "merged.json"
$resolved = Join-Path $outputRoot "resolved.json"
$snapshot = Join-Path $outputRoot "snapshot.json"
$journal = Join-Path $outputRoot "resolved.journal"
$syncLog = Join-Path $outputRoot "sync.log"
$stage = Join-Path $outputRoot "stage"
$archive = Join-Path $outputRoot "toolx-showcase.tar"
$schema = Join-Path $inputRoot "schema.json"
$base = Join-Path $inputRoot "app.base.json"
$overlay = Join-Path $inputRoot "app.local.json"
$logInput = Join-Path $inputRoot "app.log"
$packageSource = Join-Path $inputRoot "package-src"

$serverProcess = $null
try {
    Invoke-ShowcaseTool "toolx-config" @(
        "merge", "--base", $base, "--overlay", $overlay,
        "--out", $merged, "--json"
    ) "01-config-merge.json"

    Invoke-ShowcaseTool "toolx-config" @(
        "doctor", "--file", $merged, "--schema", $schema,
        "--require", "svc.host", "--expect", "svc.port=int", "--json"
    ) "02-config-doctor.json"

    Invoke-ShowcaseTool "toolx-sync" @(
        "--base", $merged, "--out", $resolved, "--schema", $schema,
        "--snapshot", $snapshot, "--journal", $journal,
        "--log-file", $syncLog, "--json"
    ) "03-sync.json"

    Invoke-ShowcaseTool "toolx-pack" @(
        "stage", "--src", $packageSource, "--out", $stage,
        "--archive", $archive, "--include", "bin",
        "--include", "README.md", "--json"
    ) "04-pack.json"

    $portFile = Join-Path $outputRoot "showcase.port"
    $server = Resolve-ShowcaseBinary "toolx_showcase_server"
    $quotedPortFile = '"{0}"' -f $portFile
    $serverProcess = Start-Process -FilePath $server -ArgumentList @(
        "--port-file", $quotedPortFile
    ) -PassThru -WindowStyle Hidden

    for ($attempt = 0; $attempt -lt 100 -and -not (Test-Path -LiteralPath $portFile); $attempt++) {
        Start-Sleep -Milliseconds 50
    }
    if (-not (Test-Path -LiteralPath $portFile)) {
        throw "Loopback server did not publish its port"
    }
    $port = (Get-Content -LiteralPath $portFile -Raw).Trim()

    Invoke-ShowcaseTool "toolx-http" @(
        "check", "--url", "http://127.0.0.1:$port/health",
        "--expect-status", "200", "--expect-body-contains", "ready",
        "--no-proxy-from-env", "--json"
    ) "05-http.json"
    if (-not $serverProcess.WaitForExit(5000)) {
        throw "Loopback server did not exit after serving the health request"
    }

    Invoke-ShowcaseTool "toolx-log" @(
        "summarize", "--file", $logInput, "--format", "logsys-text",
        "--min-level", "warning", "--json"
    ) "06-log.json"

    Push-Location $outputRoot
    try {
        Invoke-ShowcaseTool "toolx-inspect" @(
            "report", "--file", "resolved.json", "--schema", "input/schema.json", "--json"
        ) "07-inspect.json"

        Invoke-ShowcaseTool "toolx-inspect" @(
            "render", "--file", "resolved.json", "--schema", "input/schema.json",
            "--width", "90", "--height", "18"
        ) "08-inspect-frame.txt"
    }
    finally {
        Pop-Location
    }

    Push-Location $outputRoot
    try {
        Get-ChildItem -File -Recurse |
            ForEach-Object { $_.FullName.Substring($outputRoot.Length + 1).Replace('\', '/') } |
            Sort-Object |
            Set-Content -LiteralPath (Join-Path $outputRoot "artifacts.txt") -Encoding utf8
    }
    finally {
        Pop-Location
    }

    Write-Host "ToolX showcase completed: $outputRoot"
}
finally {
    if ($null -ne $serverProcess -and -not $serverProcess.HasExited) {
        Stop-Process -Id $serverProcess.Id -Force
    }
}
