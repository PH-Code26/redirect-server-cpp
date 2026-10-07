# silent start redirect service (no window)
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$logFile = Join-Path $scriptDir "service.log"

Start-Process -FilePath (Join-Path $scriptDir "url-alias-redirect.exe") `
    -WorkingDirectory $scriptDir `
    -WindowStyle Hidden `
    -RedirectStandardOutput $logFile