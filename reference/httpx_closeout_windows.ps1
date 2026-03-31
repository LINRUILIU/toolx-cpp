param(
    [string]$OpenSslRoot = 'C:/Program Files/OpenSSL-Win64',
    [string]$MbedTlsInstallPrefix = '',
    [switch]$SkipMbedTlsInstall
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-Step {
    param(
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string]$Command
    )

    Write-Host "`n==> $Title" -ForegroundColor Cyan
    Write-Host "    $Command" -ForegroundColor DarkGray
    Invoke-Expression $Command
}

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $MbedTlsInstallPrefix) {
    $MbedTlsInstallPrefix = Join-Path $RepoRoot '.third_party/mbedtls-install-x64'
}

$MbedTlsSource = Join-Path $RepoRoot '.third_party/mbedtls-3.6.2'
$MbedTlsBuild = Join-Path $MbedTlsSource 'build-x64'
$MbedTlsDir = Join-Path $MbedTlsInstallPrefix 'lib/cmake/MbedTLS'

Push-Location $RepoRoot
try {
    Invoke-Step -Title 'HTTP-only baseline (Win32) build' -Command 'cmake --build build-vs-win32 --config Debug --target httpx_tests'
    Invoke-Step -Title 'HTTP-only baseline (Win32) test' -Command 'ctest --test-dir build-vs-win32 -C Debug -R httpx_tests --output-on-failure'

    Invoke-Step -Title 'OpenSSL backend (x64) configure' -Command "cmake -S . -B build-vs-openssl-x64 -G `"Visual Studio 18 2026`" -A x64 -DHTTPX_ENABLE_OPENSSL=ON -DOPENSSL_ROOT_DIR=`"$OpenSslRoot`""
    Invoke-Step -Title 'OpenSSL backend (x64) build' -Command 'cmake --build build-vs-openssl-x64 --config Debug --target httpx_tests'
    Invoke-Step -Title 'OpenSSL backend (x64) test' -Command 'ctest --test-dir build-vs-openssl-x64 -C Debug -R httpx_tests --output-on-failure'

    if (-not $SkipMbedTlsInstall) {
        if (-not (Test-Path (Join-Path $MbedTlsSource 'framework/CMakeLists.txt'))) {
            throw "Missing $MbedTlsSource/framework/CMakeLists.txt. Keep retained dependency assets before running closure."
        }

        Invoke-Step -Title 'mbedtls local install (x64)' -Command "cmake -S `"$MbedTlsSource`" -B `"$MbedTlsBuild`" -G `"Visual Studio 18 2026`" -A x64 -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DCMAKE_INSTALL_PREFIX=`"$MbedTlsInstallPrefix`""
        Invoke-Step -Title 'mbedtls local install build' -Command "cmake --build `"$MbedTlsBuild`" --config Release --target install"
    }

    Invoke-Step -Title 'mbedtls backend (x64) configure' -Command "cmake -S . -B build-vs-mbedtls-x64 -G `"Visual Studio 18 2026`" -A x64 -DHTTPX_ENABLE_MBEDTLS=ON -DMbedTLS_DIR=`"$MbedTlsDir`""
    Invoke-Step -Title 'mbedtls backend (x64) build' -Command 'cmake --build build-vs-mbedtls-x64 --config Debug --target httpx_tests'
    Invoke-Step -Title 'mbedtls backend (x64) test' -Command 'ctest --test-dir build-vs-mbedtls-x64 -C Debug -R httpx_tests --output-on-failure'

    Write-Host "`nhttpx closure matrix passed." -ForegroundColor Green
}
finally {
    Pop-Location
}
