# silent start redirect service (no window)
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir
$logFile = Join-Path $projectRoot "log\service.log"

Start-Process -FilePath (Join-Path $projectRoot "bin\url-alias-redirect.exe") `
    -WorkingDirectory (Join-Path $projectRoot "bin") `
    -WindowStyle Hidden `
    -RedirectStandardOutput $logFile