# Vulkan Stability Test Script
param(
    [int]$NumRuns = 5,
    [int]$RunDurationSeconds = 30,
    [int]$DelayBetweenRuns = 30
)

$TestDirectory = "C:\dev\ChameleonRT\build_vulkan\Debug"
$Executable = ".\chameleonrt.exe"
$Arguments = "slang", "C:/Demo/Assets/CornellBox/CornellBox-Glossy.obj"
$LogDirectory = Join-Path $TestDirectory "stability_logs"

# Create log directory if it doesn't exist
if (!(Test-Path $LogDirectory)) {
    New-Item -ItemType Directory -Path $LogDirectory -Force
}

Write-Host "Starting Vulkan Stability Test - $NumRuns runs" -ForegroundColor Green
Write-Host "Test Directory: $TestDirectory" -ForegroundColor Cyan
Write-Host "Log Directory: $LogDirectory" -ForegroundColor Cyan

# Change to test directory
Set-Location $TestDirectory

# Results tracking
$Results = @()

for ($run = 1; $run -le $NumRuns; $run++) {
    $timestamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
    $logFile = Join-Path $LogDirectory "run_${run}_${timestamp}.log"
    $errorLogFile = Join-Path $LogDirectory "run_${run}_${timestamp}_error.log"
    
    Write-Host "`n=== Run $run/$NumRuns - Started at $(Get-Date) ===" -ForegroundColor Yellow
    Write-Host "Log file: $logFile" -ForegroundColor Gray
    
    $runResult = @{
        RunNumber = $run
        StartTime = Get-Date
        EndTime = $null
        Duration = $null
        Status = "Unknown"
        LogFile = $logFile
        ErrorLogFile = $errorLogFile
        FrameCount = 0
        CrashDetected = $false
        GracefulKill = $false
    }
    
    try {
        # Start the process
        $processArgs = @{
            FilePath = $Executable
            ArgumentList = $Arguments
            RedirectStandardOutput = $logFile
            RedirectStandardError = $errorLogFile
            NoNewWindow = $true
            PassThru = $true
        }
        
        Write-Host "Starting: $Executable $($Arguments -join ' ')" -ForegroundColor Cyan
        $process = Start-Process @processArgs
        
        # Monitor the process
        $startTime = Get-Date
        $framePattern = "Frame \d+ - Index: \d+"
        $frameCount = 0
        $lastFrameTime = $startTime
        
        while (!$process.HasExited -and ((Get-Date) - $startTime).TotalSeconds -lt $RunDurationSeconds) {
            Start-Sleep -Milliseconds 500
            
            # Check for frame messages in log file
            if (Test-Path $logFile) {
                $logContent = Get-Content $logFile -Raw -ErrorAction SilentlyContinue
                if ($logContent) {
                    $frameMatches = [regex]::Matches($logContent, $framePattern)
                    if ($frameMatches.Count -gt $frameCount) {
                        $frameCount = $frameMatches.Count
                        $lastFrameTime = Get-Date
                        Write-Host "." -NoNewline -ForegroundColor Green
                    }
                }
            }
            
            # Check if process crashed
            if ($process.HasExited) {
                $runResult.CrashDetected = $true
                break
            }
        }
        
        $runResult.EndTime = Get-Date
        $runResult.Duration = ($runResult.EndTime - $runResult.StartTime).TotalSeconds
        $runResult.FrameCount = $frameCount
        
        # Handle process termination
        if (!$process.HasExited) {
            Write-Host "`nGracefully stopping process after $RunDurationSeconds seconds..." -ForegroundColor Yellow
            
            # Try graceful shutdown first
            try {
                $process.CloseMainWindow()
                $process.WaitForExit(5000) # Wait up to 5 seconds
            }
            catch {
                Write-Host "CloseMainWindow failed, using Kill..." -ForegroundColor Orange
            }
            
            # Force kill if still running
            if (!$process.HasExited) {
                $process.Kill()
                $process.WaitForExit(2000)
            }
            
            $runResult.GracefulKill = $true
        }
        
        # Determine success criteria
        $hasEnoughFrames = $frameCount -ge 4  # At least 4 frame messages (Frame 100, 200, 300, 400)
        $didNotCrash = !$runResult.CrashDetected
        $ranLongEnough = ($runResult.Duration -ge ($RunDurationSeconds * 0.8))  # Ran for at least 80% of expected time
        
        if ($hasEnoughFrames -and $didNotCrash -and $ranLongEnough) {
            $runResult.Status = "Success"
            Write-Host "`nRun $run SUCCESS" -ForegroundColor Green
        } else {
            $runResult.Status = "Failed"
            Write-Host "`nRun $run FAILED" -ForegroundColor Red
            Write-Host "  - Enough frames ($frameCount >= 4): $hasEnoughFrames" -ForegroundColor Gray
            Write-Host "  - Did not crash: $didNotCrash" -ForegroundColor Gray
            Write-Host "  - Ran long enough ($($runResult.Duration.ToString('F1'))s >= $($RunDurationSeconds * 0.8)s): $ranLongEnough" -ForegroundColor Gray
        }
        
        Write-Host "Frames detected: $frameCount" -ForegroundColor Cyan
        Write-Host "Duration: $($runResult.Duration.ToString('F1')) seconds" -ForegroundColor Cyan
        Write-Host "Crashed: $($runResult.CrashDetected)" -ForegroundColor Cyan
        
    }
    catch {
        $runResult.Status = "Error"
        $runResult.EndTime = Get-Date
        $runResult.Duration = ($runResult.EndTime - $runResult.StartTime).TotalSeconds
        Write-Host "`nRun $run ERROR: $($_.Exception.Message)" -ForegroundColor Red
        
        # Log error details
        $errorDetails = @"
Error in Run $run at $(Get-Date):
$($_.Exception.ToString())
$($_.ScriptStackTrace)
"@
        Add-Content -Path $errorLogFile -Value $errorDetails
    }
    finally {
        # Ensure process is cleaned up
        try {
            if ($process -and !$process.HasExited) {
                $process.Kill()
                $process.Dispose()
            }
        }
        catch {
            Write-Host "Warning: Could not clean up process: $($_.Exception.Message)" -ForegroundColor Orange
        }
    }
    
    $Results += $runResult
    
    # Wait between runs (except after last run)
    if ($run -lt $NumRuns) {
        Write-Host "`nWaiting $DelayBetweenRuns seconds before next run..." -ForegroundColor Gray
        Start-Sleep -Seconds $DelayBetweenRuns
    }
}

# Generate summary report
Write-Host "`n" + "="*60 -ForegroundColor Yellow
Write-Host "VULKAN STABILITY TEST SUMMARY" -ForegroundColor Yellow
Write-Host "="*60 -ForegroundColor Yellow

$successCount = ($Results | Where-Object { $_.Status -eq "Success" }).Count
$failureCount = ($Results | Where-Object { $_.Status -eq "Failed" }).Count
$errorCount = ($Results | Where-Object { $_.Status -eq "Error" }).Count

Write-Host "Total Runs: $NumRuns" -ForegroundColor Cyan
Write-Host "Successful: $successCount" -ForegroundColor Green
Write-Host "Failed: $failureCount" -ForegroundColor Red
Write-Host "Errors: $errorCount" -ForegroundColor Magenta
Write-Host "Success Rate: $([math]::Round(($successCount / $NumRuns) * 100, 1))%" -ForegroundColor Cyan

# Detailed results
Write-Host "`nDetailed Results:" -ForegroundColor White
foreach ($result in $Results) {
    $statusColor = switch ($result.Status) {
        "Success" { "Green" }
        "Failed" { "Red" }
        "Error" { "Magenta" }
        default { "Gray" }
    }
    
    Write-Host "Run $($result.RunNumber): $($result.Status) - $($result.FrameCount) frames - $($result.Duration.ToString('F1'))s - Crashed: $($result.CrashDetected)" -ForegroundColor $statusColor
}

# Save summary to file
$summaryFile = Join-Path $LogDirectory "test_summary_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').txt"
$summaryContent = @"
Vulkan Stability Test Summary
Generated: $(Get-Date)
Test Directory: $TestDirectory
Command: $Executable $($Arguments -join ' ')

Results:
Total Runs: $NumRuns
Successful: $successCount
Failed: $failureCount  
Errors: $errorCount
Success Rate: $([math]::Round(($successCount / $NumRuns) * 100, 1))%

Detailed Results:
$($Results | ForEach-Object { "Run $($_.RunNumber): $($_.Status) - $($_.FrameCount) frames - $($_.Duration.ToString('F1'))s - Crashed: $($_.CrashDetected) - Log: $($_.LogFile)" } | Out-String)

Log Files Location: $LogDirectory
"@

Set-Content -Path $summaryFile -Value $summaryContent
Write-Host "`nSummary saved to: $summaryFile" -ForegroundColor Cyan

# Return results for further analysis
return $Results
