param(
    [Parameter(Mandatory = $true)][string]$BinDir,
    [Parameter(Mandatory = $true)][string]$WorkDir
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$utf8 = [System.Text.UTF8Encoding]::new($false, $true)
$binaryRoot = [System.IO.Path]::GetFullPath($BinDir)
$testRoot = Join-Path ([System.IO.Path]::GetFullPath($WorkDir)) ([guid]::NewGuid().ToString("N"))
$script:caseCount = 0

function Assert-Equal($actual, $expected, [string]$label) {
    if ($actual -cne $expected) {
        throw "${label}: expected '$expected', got '$actual'"
    }
}

function Write-Json([string]$path, $value) {
    [System.IO.File]::WriteAllText($path, ($value | ConvertTo-Json -Depth 20), $utf8)
}

function Read-Json([string]$path) {
    return ($utf8.GetString([System.IO.File]::ReadAllBytes($path)) | ConvertFrom-Json)
}

function Invoke-Cli([string]$name, [string[]]$arguments, [int]$expectedCode = 0) {
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $binaryRoot "$name.exe"
    # These generated arguments have no embedded quotes or trailing backslashes.
    $start.Arguments = ($arguments | ForEach-Object {
        if ($_.Contains('"') -or $_.EndsWith('\')) { throw "Unsupported test argument: $_" }
        '"{0}"' -f $_
    }) -join ' '
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $start
    $stdout = [System.IO.MemoryStream]::new()
    $stderr = [System.IO.MemoryStream]::new()
    try {
        if (-not $process.Start()) { throw "Could not start $name" }
        # Read bytes directly: PowerShell native redirection can transcode stdout
        # and hide an invalid UTF-8 stream on Windows PowerShell 5.1.
        $outTask = $process.StandardOutput.BaseStream.CopyToAsync($stdout)
        $errTask = $process.StandardError.BaseStream.CopyToAsync($stderr)
        if (-not $process.WaitForExit(15000)) {
            $process.Kill()
            throw "$name timed out"
        }
        [void]$outTask.GetAwaiter().GetResult()
        [void]$errTask.GetAwaiter().GetResult()
        $script:caseCount++
        $capture = Join-Path $testRoot ("{0:D2}-{1}" -f $script:caseCount, $name)
        [System.IO.File]::WriteAllBytes("$capture.json", $stdout.ToArray())
        [System.IO.File]::WriteAllBytes("$capture.stderr", $stderr.ToArray())
        $text = $utf8.GetString($stdout.ToArray())
        Assert-Equal $process.ExitCode $expectedCode "$name exit code; stdout=$text"
        Assert-Equal $stderr.Length 0 "$name stderr length"
        $json = $text | ConvertFrom-Json
        Assert-Equal $json.schema ("toolx.{0}.result" -f $name.Substring(6)) "$name schema"
        Assert-Equal $json.schema_version 1 "$name schema version"
        Assert-Equal $json.code $expectedCode "$name JSON code"
        Assert-Equal $json.ok ($expectedCode -eq 0) "$name JSON ok"
        return $json
    }
    finally {
        $stdout.Dispose()
        $stderr.Dispose()
        $process.Dispose()
    }
}

# Construct Unicode at runtime to also support Windows PowerShell 5.1 without
# relying on its BOM-dependent script decoding. Test both ACP-representable
# Chinese and supplementary characters that a legacy ACP cannot represent.
$chinese = [string][char]0x4E2D + [char]0x6587
$emoji = [char]::ConvertFromUtf32(0x1F680)
foreach ($label in @("$chinese paths", "$chinese $emoji paths")) {
    $root = Join-Path $testRoot $label
    [System.IO.Directory]::CreateDirectory($root) | Out-Null
    $value = "$chinese $emoji payload"
    $config = Join-Path $root "$chinese.json"
    $overlay = Join-Path $root "overlay.json"
    $merged = Join-Path $root "merged.json"
    $resolved = Join-Path $root "resolved.json"
    Write-Json $config @{ greeting = $value }
    Write-Json $overlay @{ enabled = $true }

    $json = Invoke-Cli "toolx-config" @("load", "--file", $config, "--json")
    Assert-Equal $json.data.file $config "config input path"
    Assert-Equal $json.data.config.greeting $value "UTF-8 config value"
    $json = Invoke-Cli "toolx-config" @("merge", "--base", $config, "--overlay", $overlay,
        "--out", $merged, "--json")
    Assert-Equal $json.data.out $merged "config output path"
    Assert-Equal (Read-Json $merged).greeting $value "merged content"

    $json = Invoke-Cli "toolx-sync" @("--base", $merged, "--out", $resolved, "--json")
    Assert-Equal $json.data.out $resolved "sync output path"
    Assert-Equal (Read-Json $resolved).greeting $value "published content"

    $source = Join-Path $root "source"
    $stage = Join-Path $root "stage"
    [System.IO.Directory]::CreateDirectory($source) | Out-Null
    $member = "$chinese.txt"
    [System.IO.File]::WriteAllText((Join-Path $source $member), $value, $utf8)
    $json = Invoke-Cli "toolx-pack" @("stage", "--src", $source, "--out", $stage, "--json")
    Assert-Equal $json.data.source $source "pack source path"
    Assert-Equal $json.data.stage $stage "pack stage path"
    Assert-Equal ($utf8.GetString([System.IO.File]::ReadAllBytes((Join-Path $stage $member)))) $value "staged file"

    $log = Join-Path $root "$chinese.jsonl"
    # JSONL requires one record per line.
    [System.IO.File]::WriteAllText($log,
        (@{ level = "warning"; msg = $value } | ConvertTo-Json -Compress) + "`n", $utf8)
    $json = Invoke-Cli "toolx-log" @("summarize", "--file", $log, "--format", "jsonl", "--json")
    Assert-Equal $json.data.files[0] $log "log input path"
    Assert-Equal $json.data.parsed 1 "parsed log count"
    Assert-Equal $json.data.samples[0].message $value "log message"

    $json = Invoke-Cli "toolx-inspect" @("report", "--file", $resolved, "--json")
    Assert-Equal $json.data.file $resolved "inspect input path"

    # Keep a local endpoint open without sending a response. This exercises the
    # HTTP failure envelope deterministically without external network access.
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    try {
        $manifest = Join-Path $root "$chinese-http.json"
        Write-Json $manifest @{
            checks = @(@{ name = $value; url = "http://127.0.0.1:$($listener.LocalEndpoint.Port)/" })
            timeout_ms = 100; connect_timeout_ms = 100; retry = 0
            use_proxy_from_environment = $false
        }
        $json = Invoke-Cli "toolx-http" @("check", "--manifest", $manifest, "--json") 1
        Assert-Equal $json.data.manifest $manifest "HTTP manifest path"
        Assert-Equal $json.data.checked 1 "HTTP check count"
        Assert-Equal $json.data.checks[0].name $value "HTTP check name"
        Assert-Equal $json.data.checks[0].error_kind "timeout" "HTTP timeout"
    }
    finally { $listener.Stop() }
}

Write-Host "$script:caseCount UTF-8 CLI contracts passed; raw captures: $testRoot"
