#!/usr/bin/env pwsh
<#!
.SYNOPSIS
Windows build equivalent to build.sh.
.DESCRIPTION
Compiles the DTMF toolkit using GCC/Clang on Windows. Creates build/bin folders
and emits the four helper binaries in bin/. Requires a POSIX-like compiler
(GCC from MSYS2/MinGW, clang via LLVM, or similar) available on PATH.
#>

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Resolve repo root even when invoked via relative path
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
if ([string]::IsNullOrWhiteSpace($ScriptDir)) {
    $ScriptDir = Get-Location
}
Set-Location $ScriptDir

$IncludeDir = Join-Path $ScriptDir 'include'
$BuildDir = Join-Path $ScriptDir 'build'
$BinDir = Join-Path $ScriptDir 'bin'

# Helper to run a process and surface compiler output on failure
function Invoke-Tool {
    param(
        [Parameter(Mandatory=$true)] [string] $FilePath,
        [Parameter(Mandatory=$true)] [string[]] $Arguments
    )

    $result = & $FilePath @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Command failed: $FilePath $($Arguments -join ' ')`n$result"
    }
}

New-Item -ItemType Directory -Force -Path $BuildDir, $BinDir | Out-Null

$cc = 'gcc'
$cflags = @('-Wall', '-Wextra', '-O2', "-I$IncludeDir")
$ldflags = @('-lm')

# Build the main generator
Invoke-Tool -FilePath $cc -Arguments ($cflags + @('-c', 'src/dtmf.c', '-o', (Join-Path $BuildDir 'dtmf.o')))
Invoke-Tool -FilePath $cc -Arguments ($cflags + @('-c', 'src/main.c', '-o', (Join-Path $BuildDir 'main.o')))
Invoke-Tool -FilePath $cc -Arguments (@((Join-Path $BuildDir 'dtmf.o'), (Join-Path $BuildDir 'main.o')) + $ldflags + @('-o', (Join-Path $BinDir 'dtmf-lab.exe')))

# Build the decoder CLI
Invoke-Tool -FilePath $cc -Arguments ($cflags + @('-c', 'src/decode.c', '-o', (Join-Path $BuildDir 'decode.o')))
Invoke-Tool -FilePath $cc -Arguments ($cflags + @('-c', 'src/decode_main.c', '-o', (Join-Path $BuildDir 'decode_main.o')))
Invoke-Tool -FilePath $cc -Arguments (@((Join-Path $BuildDir 'dtmf.o'), (Join-Path $BuildDir 'decode.o'), (Join-Path $BuildDir 'decode_main.o')) + $ldflags + @('-o', (Join-Path $BinDir 'dtmf-decode.exe')))

# Build the noise mixer utility
Invoke-Tool -FilePath $cc -Arguments ($cflags + @('-c', 'src/noise_mix.c', '-o', (Join-Path $BuildDir 'noise_mix.o')))
Invoke-Tool -FilePath $cc -Arguments (@((Join-Path $BuildDir 'noise_mix.o')) + $ldflags + @('-o', (Join-Path $BinDir 'noise-mix.exe')))

# Build the silence generator
Invoke-Tool -FilePath $cc -Arguments ($cflags + @('-c', 'src/silence_gen.c', '-o', (Join-Path $BuildDir 'silence_gen.o')))
Invoke-Tool -FilePath $cc -Arguments (@((Join-Path $BuildDir 'silence_gen.o'), (Join-Path $BuildDir 'dtmf.o')) + $ldflags + @('-o', (Join-Path $BinDir 'silence-gen.exe')))

Write-Host "Build complete. Binaries available under $BinDir" -ForegroundColor Green
