# =====================================================================================================
# TAPESTRY · build/killtest.ps1 · kill the writer mid-flight, then make the tape prove itself
#
# R0's gate says "1,000 kills under load lose no acknowledged entry and keep no unacknowledged one".
# The acknowledgement half of that needs the transactor's reply cache (R0.3). What this script proves
# now is the tape half, which is the half that has to hold before an acknowledgement means anything:
#
#   * after a hard kill, the tape either ends on a complete row or ends on a torn one;
#   * reopening TRUNCATES the torn bytes, counts them, and writes a `warn` entry naming the count;
#   * the reopened tape verifies with zero breaks and zero seams, positions unbroken, and the chain
#     continues from the recovered head — it never restarts at genesis (QC-1 F6);
#   * a kill never produces a tape that opens as if nothing happened.
#
# Usage:  powershell -File killtest.ps1 [-Iterations 25] [-Root C:\TAPESTRY\scratch\kill]
# =====================================================================================================
param(
    [int]$Iterations = 25,
    [string]$Root = "C:\TAPESTRY\scratch\kill",
    [string]$Exe  = "C:\TAPESTRY\bin\tapectl.exe",
    # With a flush per batch the writer spends nearly all of its time inside FlushFileBuffers, so a
    # kill almost never lands inside WriteFile and the torn-row path goes unexercised. -NoSync widens
    # the write window on purpose: it is not a durability arm, it is how the torn tail gets hit.
    [switch]$NoSync,
    [int]$Batch = 16
)

# Continue, not Stop: these tools report on stderr (row counts, refusal reasons) and a native
# command's stderr is not a PowerShell error. Every failure below is judged by an exit code.
$ErrorActionPreference = "Continue"
if (Test-Path $Root) { Remove-Item -Recurse -Force $Root }
New-Item -ItemType Directory -Force -Path $Root | Out-Null

$torn = 0; $clean = 0; $broken = 0; $recovered = 0; $rowsTotal = 0; $ghosts = 0
$rand = New-Object System.Random 20260908

for ($i = 1; $i -le $Iterations; $i++) {
    $dir = Join-Path $Root "t$i"
    New-Item -ItemType Directory -Force -Path $dir | Out-Null

    # A long write, killed while it is in flight.
    $genArgs = @("gen", "--dir", ($dir -replace '\\','/'), "--entries", "400000", "--batch", "$Batch", "--seg-bytes", "262144")
    if ($NoSync) { $genArgs += "--no-sync" }
    $p = Start-Process -FilePath $Exe -PassThru -WindowStyle Hidden -ArgumentList $genArgs
    Start-Sleep -Milliseconds $rand.Next(120, 500)
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    $p.WaitForExit()

    # What the kill left behind, before anything touches it. Two recoverable shapes, and `status`
    # tells them apart without writing: a torn row at the end of the head segment, and the ghost of a
    # roll whose batch never committed (a segment file named for a position that was never published).
    $before = (& $Exe status --dir ($dir -replace '\\','/') 2>&1) -join "`n"
    $tornBytes = 0; $ghostSegs = 0
    if ($before -match 'torn_bytes=(\d+)')      { $tornBytes = [int]$Matches[1] }
    if ($before -match 'ghost_segments=(\d+)')  { $ghostSegs = [int]$Matches[1] }
    if ($tornBytes -gt 0) { $torn++ } else { $clean++ }
    if ($ghostSegs -gt 0) { $ghosts++ }

    # Reopen (this is Tape::open: recovery, truncation, the warn entry) and append a little.
    $out = & $Exe gen --dir ($dir -replace '\\','/') --entries 5 --batch 16 --seg-bytes 262144 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  iteration ${i}: REOPEN FAILED: $out"
        $broken++
        continue
    }
    $recovered++

    $v = (& $Exe verify --dir ($dir -replace '\\','/') 2>&1) -join "`n"
    $vExit = $LASTEXITCODE
    if ($vExit -ne 0) {
        Write-Host "  iteration ${i}: VERIFY FAILED"
        Write-Host "     $v"
        $broken++
    } else {
        if ($v -match 'rows=(\d+)') { $rowsTotal += [int]$Matches[1] }
    }

    # Nothing is discarded silently: each recovery shape must leave its own named warn row, and a warn
    # row must never appear without the condition that earns it.
    $warnRows = (& $Exe dump --dir ($dir -replace '\\','/') --kind warn 2>$null) -join "`n"
    $tornWarns  = ([regex]::Matches($warnRows, 'torn_row_discarded')).Count
    $ghostWarns = ([regex]::Matches($warnRows, 'uncommitted_segment_discarded')).Count
    if (($tornBytes -gt 0) -ne ($tornWarns -ge 1)) {
        Write-Host "  iteration ${i}: torn_bytes=$tornBytes but torn warn rows=$tornWarns"
        $broken++
    }
    if (($ghostSegs -gt 0) -ne ($ghostWarns -ge 1)) {
        Write-Host "  iteration ${i}: ghost_segments=$ghostSegs but ghost warn rows=$ghostWarns"
        $broken++
    }
}

Write-Host ""
Write-Host "kills=$Iterations  killed_mid_row=$torn  killed_between_rows=$clean  uncommitted_roll=$ghosts"
Write-Host "reopened=$recovered  verified_rows_total=$rowsTotal  FAILURES=$broken"
if ($broken -gt 0) { exit 1 }
Write-Host "KILL TEST OK"
exit 0
