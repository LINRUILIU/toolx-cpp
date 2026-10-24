param(
    [Parameter(Mandatory = $true)][string]$BinDir,
    [Parameter(Mandatory = $true)][string]$WorkDir
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$junctionUtf8 = [System.Text.UTF8Encoding]::new($false, $true)
$junctionBinaryRoot = [System.IO.Path]::GetFullPath($BinDir)
$junctionRoot = Join-Path ([System.IO.Path]::GetFullPath($WorkDir)) ("audit-junctions-" + [guid]::NewGuid().ToString("N"))
[System.IO.Directory]::CreateDirectory($junctionRoot) | Out-Null
$script:junctionCaseCount = 0

function Assert-JunctionEqual($actual, $expected, [string]$label) {
    if ($actual -cne $expected) { throw "${label}: expected '$expected', got '$actual'" }
}

function Get-JunctionFileHash([string]$path) {
    $hasher = [System.Security.Cryptography.SHA256]::Create()
    try {
        return [System.BitConverter]::ToString($hasher.ComputeHash([System.IO.File]::ReadAllBytes($path)))
    }
    finally { $hasher.Dispose() }
}

function New-AuditJunction([string]$path, [string]$target) {
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $env:ComSpec
    # All paths are fresh fixtures below junctionRoot; Windows paths cannot
    # contain a quote. mklink /J requires no symbolic-link privilege.
    $start.Arguments = '/d /c mklink /J "{0}" "{1}"' -f $path, $target
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        Assert-JunctionEqual $process.ExitCode 0 "mklink /J; $stdout $stderr"
        $attributes = [System.IO.File]::GetAttributes($path)
        if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) {
            throw "Fixture is not a reparse point: $path"
        }
    }
    finally { $process.Dispose() }
}

function Invoke-JunctionCli([string]$name, [string[]]$arguments, [int]$expectedCode, [string]$label) {
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $junctionBinaryRoot "$name.exe"
    $start.Arguments = ($arguments | ForEach-Object {
        if ($_.Contains('"') -or $_.EndsWith('\')) { throw "Unsupported test argument: $_" }
        '"{0}"' -f $_
    }) -join ' '
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = $junctionUtf8
    $start.StandardErrorEncoding = $junctionUtf8
    $process = [System.Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(15000)) { $process.Kill(); throw "$label timed out" }
        $text = $stdout.GetAwaiter().GetResult()
        $errorText = $stderr.GetAwaiter().GetResult()
        $script:junctionCaseCount++
        [System.IO.File]::WriteAllText((Join-Path $junctionRoot "$label.json"), $text, $junctionUtf8)
        Assert-JunctionEqual $process.ExitCode $expectedCode "$label exit; stdout=$text stderr=$errorText"
        $json = $text | ConvertFrom-Json
        Assert-JunctionEqual $json.code $expectedCode "$label JSON code"
        Assert-JunctionEqual $json.ok ($expectedCode -eq 0) "$label JSON ok"
        if ($expectedCode -eq 2 -and $json.message -notmatch 'cannot confirm|overlaps|aliases') {
            throw "$label failed for an unrelated reason: $text"
        }
    }
    finally { $process.Dispose() }
}

# A listener that never replies keeps HTTP controls local and bounded. Every
# rejected HTTP path must fail before attempting a connection.
$junctionListener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$junctionListener.Start()
try {
    foreach ($name in @("toolx-pack", "toolx-sync", "toolx-http", "toolx-log", "toolx-inspect")) {
        $root = Join-Path $junctionRoot $name
        $source = Join-Path $root "source"
        $published = Join-Path $root "published"
        [System.IO.Directory]::CreateDirectory($source) | Out-Null
        [System.IO.File]::WriteAllText((Join-Path $source "a.txt"), "SOURCE-SENTINEL", $junctionUtf8)
        $input = Join-Path $source "input.json"
        [System.IO.File]::WriteAllText($input, '{"value":"INPUT-SENTINEL"}', $junctionUtf8)
        $member = "audit.log"
        $expectedCode = 0
        switch ($name) {
            "toolx-pack" {
                $arguments = @("stage", "--src", $source, "--out", $published)
                $member = "a.txt"
                $protectedMember = "a.txt"
            }
            "toolx-sync" {
                $arguments = @("--base", $input, "--out", (Join-Path $published "output.json"))
                $member = "output.json"
                $protectedMember = "input.json"
            }
            "toolx-http" {
                $manifest = @{ checks = @(@{ url = "http://127.0.0.1:$($junctionListener.LocalEndpoint.Port)/" })
                    timeout_ms = 100; connect_timeout_ms = 100; retry = 0; use_proxy_from_environment = $false }
                [System.IO.File]::WriteAllText($input, ($manifest | ConvertTo-Json -Depth 10), $junctionUtf8)
                $arguments = @("check", "--manifest", $input)
                $protectedMember = "input.json"
                $expectedCode = 1
            }
            "toolx-log" {
                $input = Join-Path $source "input.jsonl"
                [System.IO.File]::WriteAllText($input, "{`"level`":`"info`",`"msg`":`"INPUT-SENTINEL`"}`n", $junctionUtf8)
                $arguments = @("summarize", "--file", $input, "--format", "jsonl")
                $protectedMember = "input.jsonl"
            }
            "toolx-inspect" {
                $arguments = @("report", "--file", $input)
                $protectedMember = "input.json"
            }
        }
        $before = Get-JunctionFileHash $input
        $alias = Join-Path $root "dangling-alias"
        New-AuditJunction $alias $published
        Invoke-JunctionCli $name ($arguments + @("--log-file", (Join-Path $alias $member), "--json")) 2 "$name-dangling-main"
        Assert-JunctionEqual ([System.IO.Directory]::Exists($published)) $false "$name created missing junction target"
        Assert-JunctionEqual (Get-JunctionFileHash $input) $before "$name changed input"

        $rotation = Join-Path $root "rotation.log"
        $rotationTarget = Join-Path $root "missing-rotation-target"
        New-AuditJunction "$rotation.1" $rotationTarget
        Invoke-JunctionCli $name ($arguments + @("--log-file", $rotation, "--json")) 2 "$name-dangling-rotation"
        Assert-JunctionEqual ([System.IO.File]::Exists($rotation)) $false "$name initialized rejected audit log"
        Assert-JunctionEqual ([System.IO.Directory]::Exists($rotationTarget)) $false "$name created rotation target"
        Assert-JunctionEqual ([System.IO.Directory]::Exists($published)) $false "$name published rejected output"

        $protectedAlias = Join-Path $root "protected-alias"
        New-AuditJunction $protectedAlias $source
        Invoke-JunctionCli $name ($arguments + @("--log-file", (Join-Path $protectedAlias $protectedMember), "--json")) 2 "$name-existing-alias"
        Assert-JunctionEqual (Get-JunctionFileHash $input) $before "$name changed protected input"
        if ($name -eq "toolx-http") {
            Assert-JunctionEqual $junctionListener.Pending() $false "HTTP rejected path attempted a connection"
        }

        $ordinaryLog = Join-Path $root "ordinary-new-audit.log"
        Invoke-JunctionCli $name ($arguments + @("--log-file", $ordinaryLog, "--json")) $expectedCode "$name-ordinary-control"
        Assert-JunctionEqual ([System.IO.File]::Exists($ordinaryLog)) $true "$name ordinary audit missing"
        $logStore = Join-Path $root "log-store"
        [System.IO.Directory]::CreateDirectory($logStore) | Out-Null
        $normalAlias = Join-Path $root "normal-alias"
        New-AuditJunction $normalAlias $logStore
        Invoke-JunctionCli $name ($arguments + @("--log-file", (Join-Path $normalAlias "audit.log"), "--json")) $expectedCode "$name-junction-control"
        Assert-JunctionEqual ([System.IO.File]::Exists((Join-Path $logStore "audit.log"))) $true "$name junction audit missing"
        Assert-JunctionEqual (Get-JunctionFileHash $input) $before "$name control changed input"
        if ($name -eq "toolx-pack") {
            Assert-JunctionEqual ([System.IO.File]::ReadAllText((Join-Path $published "a.txt"), $junctionUtf8)) "SOURCE-SENTINEL" "pack control stage pollution"
        }
        if ($name -eq "toolx-sync") {
            $output = [System.IO.File]::ReadAllText((Join-Path $published "output.json"), $junctionUtf8) | ConvertFrom-Json
            Assert-JunctionEqual $output.value "INPUT-SENTINEL" "sync control output pollution"
        }
    }
}
finally { $junctionListener.Stop() }
Write-Host "$script:junctionCaseCount Windows audit junction contracts passed; captures: $junctionRoot"
