#!/usr/bin/env pwsh
<#!
.SYNOPSIS
Build and run the full evaluation pipeline on Windows.
.DESCRIPTION
Runs the Windows build, generates noisy test fixtures, then evaluates decoder accuracy
in one command. Requires PowerShell, gcc/clang on PATH, and python3 available.
#>

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot = Split-Path -Parent $ScriptDir

# Paths
$CodesFile = Join-Path $RepoRoot 'testdata/codes.txt'
$AtcDir = Join-Path $RepoRoot 'testdata/atc'
$OutputDir = Join-Path $RepoRoot 'artifacts/wav/tests'
$SilenceMs = 3000
$SilenceRate = 8000
$ToneMs = 100
$DenseGap = 60
$SparseGap = 400
$WhiteSnrSubset = @(20, 10, 5, 0)
$AtcSnrSubset = @(20, 10, 5)

# Executables
$SilenceGen = Join-Path $RepoRoot 'bin/silence-gen.exe'
$NoiseMix = Join-Path $RepoRoot 'bin/noise-mix.exe'
$Generator = Join-Path $RepoRoot 'bin/dtmf-lab.exe'

function Invoke-Step {
    param(
        [Parameter(Mandatory=$true)] [string] $Message,
        [Parameter(Mandatory=$true)] [scriptblock] $Action
    )

    Write-Host $Message -ForegroundColor Cyan
    & $Action
}

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

function Get-Codes {
    Get-Content -Path $CodesFile | ForEach-Object {
        $line = $_.Trim()
        if ($line -and -not $line.StartsWith('#')) {
            $line
        }
    }
}

function Sanitize-Code {
    param([string] $Code)
    $code = $Code -replace '\*', 'star'
    $code = $code -replace '#', 'hash'
    return $code
}

function Ensure-Silence {
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    $silencePath = Join-Path $OutputDir "silence__${SilenceMs}ms.wav"
    if (-not (Test-Path $silencePath)) {
        Write-Host "[silence] creating $SilenceMs ms silence -> $silencePath"
        Invoke-Tool -FilePath $SilenceGen -Arguments @('-o', $silencePath, '--duration-ms', "$SilenceMs", '--sample-rate', "$SilenceRate")
    }
    return $silencePath
}

function Generate-NoiseOnly {
    param([string] $SilencePath)
    foreach ($snr in $WhiteSnrSubset) {
        $outPath = Join-Path $OutputDir "white__noise_only__snr_${snr}dB.wav"
        Write-Host "[white noise-only] snr=$snr dB -> $outPath"
        Invoke-Tool -FilePath $NoiseMix -Arguments @('-i', $SilencePath, '-o', $outPath, '--snr-db', "$snr", '--mode', 'white')
    }

    if (Test-Path $AtcDir) {
        Get-ChildItem -Path $AtcDir -Filter '*.wav' | ForEach-Object {
            $noiseBase = $_.BaseName
            $outPath = Join-Path $OutputDir "atc__noise_only__noise_${noiseBase}.wav"
            Write-Host "[atc noise-only] noise=$noiseBase -> $outPath"
            Invoke-Tool -FilePath $NoiseMix -Arguments @('-i', $SilencePath, '-o', $outPath, '--snr-db', '0', '--noise-wav', $_.FullName)
        }
    }
}

function Generate-CodeVariants {
    param([string] $SilencePath)
    foreach ($code in Get-Codes) {
        $safeCode = Sanitize-Code -Code $code
        $denseClean = Join-Path $OutputDir "clean__dense__code_${safeCode}.wav"
        $sparseClean = Join-Path $OutputDir "clean__sparse__code_${safeCode}.wav"

        Write-Host "[clean dense] code=$code -> $denseClean"
        Invoke-Tool -FilePath $Generator -Arguments @('-d', "$ToneMs", '-g', "$DenseGap", '-o', $denseClean, $code)

        Write-Host "[clean sparse] code=$code -> $sparseClean"
        Invoke-Tool -FilePath $Generator -Arguments @('-d', "$ToneMs", '-g', "$SparseGap", '-o', $sparseClean, $code)

        foreach ($snr in $WhiteSnrSubset) {
            $outPath = Join-Path $OutputDir "white__dense__code_${safeCode}__snr_${snr}dB.wav"
            Write-Host "[white dense] code=$code, snr=$snr dB -> $outPath"
            Invoke-Tool -FilePath $NoiseMix -Arguments @('-i', $denseClean, '-o', $outPath, '--snr-db', "$snr", '--mode', 'white')
        }

        if (Test-Path $AtcDir) {
            Get-ChildItem -Path $AtcDir -Filter '*.wav' | ForEach-Object {
                $noiseBase = $_.BaseName
                foreach ($snr in $AtcSnrSubset) {
                    $outPath = Join-Path $OutputDir "atc__dense__code_${safeCode}__noise_${noiseBase}__snr_${snr}dB.wav"
                    Write-Host "[atc dense]   code=$code, noise=$noiseBase, snr=$snr dB -> $outPath"
                    Invoke-Tool -FilePath $NoiseMix -Arguments @('-i', $denseClean, '-o', $outPath, '--snr-db', "$snr", '--noise-wav', $_.FullName)
                }
            }
        }
    }
}

Invoke-Step -Message '[1/3] Building binaries...' -Action {
    & (Join-Path $RepoRoot 'build.ps1')
}

$silence = Invoke-Step -Message "[2/3] Generating evaluation fixtures..." -Action {
    $path = Ensure-Silence
    Generate-NoiseOnly -SilencePath $path
    Generate-CodeVariants -SilencePath $path
    return $path
}

Invoke-Step -Message '[3/3] Running decoder evaluation...' -Action {
    & python3 (Join-Path $RepoRoot 'tools/evaluate_dtmf.py')
}
