param(
    [Parameter(Mandatory = $true)]
    [string]$RedisCliPath
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$results = "$root\tests\results\login-chat"
$redisPort = if ($env:PORTFOLIO_REDIS_PORT) { [int]$env:PORTFOLIO_REDIS_PORT } else { 6379 }
$dbPort = if ($env:PORTFOLIO_DB_PORT) { [int]$env:PORTFOLIO_DB_PORT } else { 3306 }
$processes = @()

function Wait-Port([int]$port) {
    for ($i = 0; $i -lt 100; ++$i) {
        if (Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue) { return }
        Start-Sleep -Milliseconds 100
    }
    throw "Port $port did not open."
}

try {
    Wait-Port $dbPort
    Wait-Port $redisPort
    New-Item -ItemType Directory -Path $results -Force | Out-Null

    $login = Start-Process "$root\projects\login-chat-system\LoginServer\x64\Release\003_Login_Server.exe" `
        -WorkingDirectory $results -WindowStyle Hidden -PassThru
    $chat = Start-Process "$root\projects\login-chat-system\ChatServer\x64\Release\002_Multi_Thread_Chating_Server.exe" `
        -WorkingDirectory $results -WindowStyle Hidden -PassThru
    $processes += $login
    $processes += $chat

    Wait-Port 6001
    Wait-Port 6000
    Add-Type -Path "$root\tests\ProtocolProbe.cs"
    $result = [ProtocolProbe]::FullIntegration(6001, 6000, 16)
    $dbSize = (& $RedisCliPath -p $redisPort DBSIZE | Select-Object -Last 1).Trim()
    if ($dbSize -ne '0') { throw "Expected an isolated Redis with DBSIZE 0 after the test." }

    "$result redisDbSize=$dbSize"
} finally {
    foreach ($process in $processes) {
        if ($process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id
            $process.WaitForExit(5000) | Out-Null
        }
    }
}
