$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$executables = @(
    "$root\projects\memory-pool\x64\Debug\MemoryPool.exe",
    "$root\projects\memory-pool\x64\Release\MemoryPool.exe",
    "$root\projects\lock-free-queue\x64\Debug\LockFreeQueue.exe",
    "$root\projects\lock-free-queue\x64\Release\LockFreeQueue.exe"
)

foreach ($executable in $executables) {
    if (-not (Test-Path -LiteralPath $executable)) { throw "Build output missing: $executable" }
    & $executable
    if ($LASTEXITCODE -ne 0) { throw "Test failed: $executable" }
}
