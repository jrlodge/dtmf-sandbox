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
$JitterTones = @(60, 80, 120, 150)
$JitterGaps = @(60, 100, 250, 400)
$OffsetSilenceMax = 1000
$BurstSilenceMs = 500
$WhiteSnrSubset = @(20, 10, 5, 0)
$AtcSnrSubset = @(20, 10, 5)
$ComplexSnrSubset = @(10, 5)
$ComplexNoiseSets = @('white+ATIS_Schiphol', 'white+Inner_marker', 'ATIS_Schiphol+Inner_marker')

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

function Get-RandomChoice {
    param([object[]] $Values)
    return Get-Random -InputObject $Values
}

function Ensure-SilenceDuration {
    param([int] $DurationMs)
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
    $silencePath = Join-Path $OutputDir "silence__${DurationMs}ms.wav"
    if (-not (Test-Path $silencePath)) {
        Write-Host "[silence] creating $DurationMs ms silence -> $silencePath"
        Invoke-Tool -FilePath $SilenceGen -Arguments @('-o', $silencePath, '--duration-ms', "$DurationMs", '--sample-rate', "$SilenceRate")
    }
    return $silencePath
}

function Ensure-Silence {
    return Ensure-SilenceDuration -DurationMs $SilenceMs
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

function Invoke-ConcatWavs {
    param(
        [Parameter(Mandatory=$true)] [string] $Output,
        [Parameter(Mandatory=$true)] [string[]] $Inputs
    )

    Invoke-Tool -FilePath 'python3' -Arguments @((Join-Path $RepoRoot 'tools/wav_concat.py'), '-o', $Output) + $Inputs
}

function Invoke-BurstyNoise {
    param(
        [Parameter(Mandatory=$true)] [string] $Base,
        [Parameter(Mandatory=$true)] [string] $Out,
        [Parameter(Mandatory=$true)] [double] $Snr,
        [string] $Noise,
        [string] $NoiseWav
    )

    $args = @((Join-Path $RepoRoot 'tools/bursty_noise_overlay.py'), '--base', $Base, '--out', $Out, '--snr-db', "$Snr")
    if ($Noise) {
        $args += @('--noise', $Noise)
    } elseif ($NoiseWav) {
        $args += @('--noise-wav', $NoiseWav)
    }
    Invoke-Tool -FilePath 'python3' -Arguments $args
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

        $jitterTone = Get-RandomChoice -Values $JitterTones
        $jitterGap = Get-RandomChoice -Values $JitterGaps
        $jitterPath = Join-Path $OutputDir "clean__jitter__code_${safeCode}.wav"
        Write-Host "[clean jitter] code=$code, tone=$jitterTone ms, gap=$jitterGap ms -> $jitterPath"
        Invoke-Tool -FilePath $Generator -Arguments @('-d', "$jitterTone", '-g', "$jitterGap", '-o', $jitterPath, $code)

        $leadMs = Get-Random -Minimum 0 -Maximum ($OffsetSilenceMax + 1)
        $trailMs = Get-Random -Minimum 0 -Maximum ($OffsetSilenceMax + 1)
        $leadSilence = Ensure-SilenceDuration -DurationMs $leadMs
        $trailSilence = Ensure-SilenceDuration -DurationMs $trailMs
        $offsetPath = Join-Path $OutputDir "clean__offset__code_${safeCode}.wav"
        Write-Host "[clean offset] code=$code, lead=$leadMs ms, trail=$trailMs ms -> $offsetPath"
        Invoke-ConcatWavs -Output $offsetPath -Inputs @($leadSilence, $denseClean, $trailSilence)

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

        foreach ($combo in $ComplexNoiseSets) {
            foreach ($snr in $ComplexSnrSubset) {
                $comboOut = Join-Path $OutputDir "complex__dense__code_${safeCode}__noise_${combo}__snr_${snr}dB.wav"
                Write-Host "[complex] code=$code, combo=$combo, snr=$snr dB -> $comboOut"

                $parts = $combo -split '\+'
                $current = $denseClean
                $tmpFiles = @()
                $success = $true

                foreach ($noiseName in $parts) {
                    if (-not $noiseName) { continue }
                    $tmpFile = [System.IO.Path]::GetTempFileName()
                    $tmpWav = [System.IO.Path]::ChangeExtension($tmpFile, '.wav')
                    Move-Item -Force -Path $tmpFile -Destination $tmpWav
                    $tmpFiles += $tmpWav

                    if ($noiseName -eq 'white') {
                        Invoke-Tool -FilePath $NoiseMix -Arguments @('-i', $current, '-o', $tmpWav, '--snr-db', "$snr", '--mode', 'white')
                    } else {
                        $noisePath = Join-Path $AtcDir "$noiseName.wav"
                        if (-not (Test-Path $noisePath)) {
                            Write-Warning "[complex] skipping combo=$combo; missing $noiseName.wav"
                            $success = $false
                            break
                        }
                        Invoke-Tool -FilePath $NoiseMix -Arguments @('-i', $current, '-o', $tmpWav, '--snr-db', "$snr", '--noise-wav', $noisePath)
                    }

                    $current = $tmpWav
                }

                if ($success -and (Test-Path $current)) {
                    Move-Item -Force -Path $current -Destination $comboOut
                    foreach ($tmp in $tmpFiles) {
                        if (Test-Path $tmp) { Remove-Item -Force $tmp }
                    }
                } else {
                    foreach ($tmp in $tmpFiles) {
                        if (Test-Path $tmp) { Remove-Item -Force $tmp }
                    }
                }
            }
        }

        $burstBase = Join-Path $OutputDir "bursty_base__code_${safeCode}.wav"
        $leadBurst = Ensure-SilenceDuration -DurationMs $BurstSilenceMs
        $trailBurst = Ensure-SilenceDuration -DurationMs $BurstSilenceMs
        Invoke-ConcatWavs -Output $burstBase -Inputs @($leadBurst, $denseClean, $trailBurst)

        foreach ($snr in $AtcSnrSubset) {
            $burstWhiteOut = Join-Path $OutputDir "bursty__code_${safeCode}__noise_white__snr_${snr}dB.wav"
            Write-Host "[bursty] code=$code, noise=white, snr=$snr dB -> $burstWhiteOut"
            Invoke-BurstyNoise -Base $burstBase -Out $burstWhiteOut -Snr $snr -Noise 'white'
        }

        if (Test-Path $AtcDir) {
            Get-ChildItem -Path $AtcDir -Filter '*.wav' | ForEach-Object {
                $noiseBase = $_.BaseName
                foreach ($snr in $AtcSnrSubset) {
                    $burstAtcOut = Join-Path $OutputDir "bursty__code_${safeCode}__noise_${noiseBase}__snr_${snr}dB.wav"
                    Write-Host "[bursty] code=$code, noise=$noiseBase, snr=$snr dB -> $burstAtcOut"
                    Invoke-BurstyNoise -Base $burstBase -Out $burstAtcOut -Snr $snr -NoiseWav $_.FullName
                }
            }
        }

        if (Test-Path $burstBase) { Remove-Item -Force $burstBase }
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
