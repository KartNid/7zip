param([Parameter(Mandatory)][string]$Installer)
$ErrorActionPreference = 'Stop'
$Installer = (Resolve-Path $Installer).Path
$testRoot = Join-Path $env:RUNNER_TEMP '7zip installer smoke'
if (Test-Path $testRoot) { throw "Test directory already exists: $testRoot" }
$installDir = Join-Path $testRoot 'Custom App Location'
New-Item -ItemType Directory -Force $testRoot | Out-Null
$classes = 'HKLM:\Software\Classes'
$clsid = '{23170F69-40C1-278A-1000-000100020000}'
$inproc = "$classes\CLSID\$clsid\InprocServer32"

function Install([string]$Tasks) {
    $result = Start-Process $Installer -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/LANG=zhcn', ('/TASKS="' + $Tasks + '"'), ('/DIR="' + $installDir + '"'), ('/LOG="' + $testRoot + '\install.log"') -Wait -PassThru
    if ($result.ExitCode -ne 0) { throw "Silent installation failed: $($result.ExitCode)" }
}
function Uninstall {
    $result = Start-Process (Join-Path $installDir 'unins000.exe') -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait -PassThru
    if ($result.ExitCode -ne 0) { throw "Silent uninstall failed: $($result.ExitCode)" }
    $deadline = (Get-Date).AddSeconds(15)
    do { Start-Sleep -Milliseconds 250 } until (-not (Test-Path (Join-Path $installDir 'unins000.exe')) -or (Get-Date) -gt $deadline)
}

try {
    Install ''
    $registered = Get-Item -LiteralPath $inproc -ErrorAction SilentlyContinue
    if ($registered -and $registered.GetValue('') -eq (Join-Path $installDir '7-zip.dll')) { throw 'Unchecked shell task registered extension' }
    Uninstall
    Write-Output 'PASS install without shell integration and uninstall'

    Install 'shell,associations'
    if ((Get-ChildItem "$installDir/Lang" -File).Count -ne 93) { throw 'Incomplete installed language resources' }
    if ((Get-Item $inproc).GetValue('') -ne (Join-Path $installDir '7-zip.dll')) { throw 'COM extension path is incorrect' }
    foreach ($handler in @('*\shellex\ContextMenuHandlers', 'Directory\shellex\ContextMenuHandlers', 'Folder\shellex\ContextMenuHandlers', 'Directory\shellex\DragDropHandlers', 'Drive\shellex\DragDropHandlers')) {
        if ((Get-Item -LiteralPath "$classes\$handler\7-Zip").GetValue('') -ne $clsid) { throw "Handler missing: $handler" }
    }
    foreach ($exe in @('7zFM.exe', '7zG.exe')) {
        $path = "HKLM:\Software\Microsoft\Windows\CurrentVersion\App Paths\$exe"
        if ((Get-Item $path).GetValue('') -ne (Join-Path $installDir $exe)) { throw "App Path incorrect: $exe" }
    }
    foreach ($extension in @('.lz4', '.lz4l')) {
        $key = Get-Item "$classes\$extension\OpenWithProgids"
        if ('SevenZipLZ4' -notin $key.GetValueNames()) { throw "Open with registration missing: $extension" }
    }
    Write-Output 'PASS custom directory, 93 languages, COM/handler/App Paths and Open with registration'

    # Run in a child STA process so the COM shell DLL is unloaded before upgrade.
    & powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "$PSScriptRoot/windows_shell.ps1" -InstallDir $installDir -TestDir "$testRoot/data"
    if ($LASTEXITCODE -ne 0) { throw 'Live GUI/shell tests failed' }
    & python "$PSScriptRoot/lz4_smoke.py" (Join-Path $installDir '7z.exe') --reference
    if ($LASTEXITCODE -ne 0) { throw 'Installed DLL console regression failed' }

    # Same AppId upgrades the custom path. Deselecting shell must take effect.
    Install ''
    if (Test-Path $inproc) { throw 'Upgrade with shell disabled left own COM registration' }
    Uninstall
    if (Test-Path "$installDir/7zFM.exe") { throw 'Uninstall left application binaries' }
    if (Test-Path "$installDir/Lang") { throw 'Uninstall left language resources' }
    if (Test-Path "$classes\SevenZipLZ4") { throw 'Uninstall left Open with ProgID' }
    Write-Output 'PASS upgrade, shell opt-out, and uninstall cleanup'
} finally {
    # CI runs on a disposable Windows VM, not the developer machine.
    Get-Process 7zFM, 7zG -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    if (Test-Path "$installDir/unins000.exe") { Uninstall }
}
