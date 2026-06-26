$port = New-Object System.IO.Ports.SerialPort COM11, 9600, None, 8, one
$port.ReadTimeout = 2000
try {
    $port.Open()
    Write-Host "Listening on COM11 at 9600 Baud for 20 seconds..."
    $start = Get-Date
    while (((Get-Date) - $start).TotalSeconds -lt 20) {
        if ($port.BytesToRead -gt 0) {
            $line = $port.ReadLine()
            Write-Output $line
        } else {
            Start-Sleep -Milliseconds 100
        }
    }
} catch {
    Write-Host "Stopped: $_"
} finally {
    if ($port.IsOpen) {
        $port.Close()
    }
}
