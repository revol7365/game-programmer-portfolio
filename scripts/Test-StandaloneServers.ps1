$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$results = "$root\tests\results\standalone"
New-Item -ItemType Directory -Path $results -Force | Out-Null
Add-Type -Path "$root\tests\ProtocolProbe.cs"

function Wait-Port([int]$port) {
    for ($i = 0; $i -lt 100; ++$i) {
        try {
            $client = [Net.Sockets.TcpClient]::new()
            $client.Connect('127.0.0.1', $port)
            $client.Close()
            return
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    throw "Port $port did not open."
}

function Invoke-ServerProbe([string]$name, [string]$executable, [int]$port, [scriptblock]$probe) {
    $process = $null
    try {
        $process = Start-Process $executable -WorkingDirectory $results -WindowStyle Hidden `
            -RedirectStandardOutput "$results\$name.stdout.log" `
            -RedirectStandardError "$results\$name.stderr.log" -PassThru
        $null = $process.Handle
        Wait-Port $port
        $result = & $probe
        $result | Set-Content "$results\$name.result.txt" -Encoding utf8
        Write-Output $result
    } finally {
        if ($process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id
            $process.WaitForExit(5000) | Out-Null
        }
    }
}

Invoke-ServerProbe 'chat' `
    "$root\projects\multithread-chat-server\Server\x64\Release\002_Multi_Thread_Chating_Server.exe" `
    6000 { [ProtocolProbe]::Run('Chat', 6000, 16) }

Invoke-ServerProbe 'group-echo' `
    "$root\projects\group-echo-server\Server\x64\Release\004_Echo_Group_Server.exe" `
    16003 { [ProtocolProbe]::Run('GroupEcho', 16003, 16) }
