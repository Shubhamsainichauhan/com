$portName = "COM11"
$baudRate = 115200
$outputFile = "C:\Users\anils\LoRa_Image_CCSDS\received_image.ccsds"

$port = New-Object System.IO.Ports.SerialPort $portName, $baudRate, None, 8, one
$port.ReadTimeout = 10000

try {
    $port.Open()
    Write-Host "=================================================="
    Write-Host "      PowerShell CCSDS Image Reassembler          "
    Write-Host "=================================================="
    Write-Host "Listening on $portName at $baudRate baud..."
    
    $activeFileId = $null
    $totalChunks = 0
    $receivedChunks = @{}
    
    $lastReadTime = Get-Date
    
    while ($true) {
        if ($port.BytesToRead -gt 0) {
            $line = $port.ReadLine().Trim()
            $lastReadTime = Get-Date
            
            if ($line.StartsWith("[DATA]")) {
                $payloadPart = $line.Substring(7) # Skip "[DATA] "
                $parts = $payloadPart.Split(":")
                if ($parts.Length -eq 2) {
                    $metaStr = $parts[0]
                    $hexStr = $parts[1]
                    
                    $metaParts = $metaStr.Split(",")
                    if ($metaParts.Length -eq 4) {
                        $fileId = [int]$metaParts[0]
                        $chunkIdx = [int]$metaParts[1]
                        $totChunks = [int]$metaParts[2]
                        $chunkSize = [int]$metaParts[3]
                        
                        if ($null -eq $activeFileId -or $fileId -ne $activeFileId) {
                            Write-Host "`n[INIT] Synced with Transmitter! Waiting for chunks..."
                            $activeFileId = $fileId
                            $totalChunks = $totChunks
                            $receivedChunks = @{}
                        }
                        
                        # Convert hex to bytes using SoapHexBinary
                        $bytes = [System.Runtime.Remoting.Metadata.W3cXsd2001.SoapHexBinary]::Parse($hexStr).Value
                        $receivedChunks[$chunkIdx] = $bytes
                        
                        $receivedCount = $receivedChunks.Count
                        $percent = ($receivedCount / $totalChunks) * 100
                        
                        Write-Host -NoNewline "`rProgress: $receivedCount/$totalChunks ($($percent.ToString('F1'))%) Chunks received"
                        
                        if ($receivedCount -eq $totalChunks) {
                            Write-Host "`n`n🎉 All $totalChunks chunks successfully received!"
                            
                            # Reassemble
                            $finalBytes = New-Object Byte[] 0
                            for ($idx = 0; $idx -lt $totalChunks; $idx++) {
                                $finalBytes += $receivedChunks[$idx]
                            }
                            
                            [IO.File]::WriteAllBytes($outputFile, $finalBytes)
                            Write-Host "[SAVED] Real image successfully saved to: $outputFile"
                            break # Exit loop
                        }
                    }
                }
            } elseif ($line.StartsWith("[DEBUG]")) {
                Write-Host "`n$line"
            }
        } else {
            Start-Sleep -Milliseconds 100
            if (((Get-Date) - $lastReadTime).TotalSeconds -gt 60) {
                Write-Host "`n[TIMEOUT] No packets received for 60 seconds. Exiting."
                break
            }
        }
    }
} catch {
    Write-Host "`nError: $_"
} finally {
    if ($port.IsOpen) {
        $port.Close()
    }
}
