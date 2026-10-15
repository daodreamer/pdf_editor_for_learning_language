param([Parameter(Mandatory)][int]$TargetProcessId, [Parameter(Mandatory)][string]$OutputPath)
$ErrorActionPreference = 'Stop'
$targetProcess = Get-Process -Id $TargetProcessId
if ($targetProcess.ProcessName -ne 'reading_tests') { throw 'Expected the explicitly selected reading_tests process' }
$targetStart = $targetProcess.StartTime
$stream = [System.IO.File]::Open($OutputPath, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
$writer = [System.IO.StreamWriter]::new($stream)
$timer = [System.Diagnostics.Stopwatch]::StartNew()
try {
    while ($true) {
        $currentProcess = Get-Process -Id $TargetProcessId -ErrorAction SilentlyContinue
        if (!$currentProcess -or $currentProcess.StartTime -ne $targetStart) { break }
        try {
            $sample = Get-Counter -Counter '\GPU Process Memory(*)\Dedicated Usage','\GPU Process Memory(*)\Shared Usage','\GPU Process Memory(*)\Total Committed'
            $matching = @($sample.CounterSamples | Where-Object { $_.InstanceName -like "pid_${TargetProcessId}_*" })
            $row = [ordered]@{ elapsed_ms = $timer.Elapsed.TotalMilliseconds; pid = $TargetProcessId; instances = $matching.Count }
            foreach ($metric in @('dedicated usage','shared usage','total committed')) {
                $values = @($matching | Where-Object { $_.Path.EndsWith("\$metric") })
                $row[$metric] = if ($values.Count) { ($values | Measure-Object -Property CookedValue -Sum).Sum } else { $null }
            }
            $writer.WriteLine(($row | ConvertTo-Json -Compress))
        } catch {
            $writer.WriteLine(([ordered]@{ elapsed_ms=$timer.Elapsed.TotalMilliseconds; error=$_.Exception.Message } | ConvertTo-Json -Compress))
        }
        $writer.Flush()
        Start-Sleep -Seconds 5
    }
} finally { $writer.Dispose(); $stream.Dispose() }
