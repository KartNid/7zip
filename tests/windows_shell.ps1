param([Parameter(Mandatory)][string]$InstallDir, [Parameter(Mandatory)][string]$TestDir)
$ErrorActionPreference = 'Stop'
Add-Type -Path "$PSScriptRoot/windows_shell.cs" -ReferencedAssemblies System.Windows.Forms
$expectedFiles = @('7zz.exe', '7za.exe', '7z.exe', '7z.dll', '7zFM.exe', '7zG.exe', '7-zip.dll')
foreach ($name in $expectedFiles) {
    if (-not (Test-Path (Join-Path $InstallDir $name))) { throw "Missing installed binary: $name" }
}
foreach ($name in @('7z.dll', '7zFM.exe')) {
    $resource = [ShellSmoke]::Resource((Join-Path $InstallDir $name))
    if ($resource -notmatch '\blz4\b' -or $resource -notmatch '\blz4l\b') { throw "LZ4 association missing from $name" }
}
Write-Output 'PASS installed native .lz4/.lz4l association resources'

$settings = 'HKCU:\Software\7-Zip'
$hadLang = $null -ne (Get-ItemProperty $settings -Name Lang -ErrorAction SilentlyContinue)
$oldLang = if ($hadLang) { (Get-ItemProperty $settings -Name Lang).Lang } else { $null }
try {
    foreach ($language in @(@('zh-cn', '文件'), @('zh-tw', '檔案'), @('de', 'Datei'), @('-', 'File'))) {
        New-Item $settings -Force | Out-Null
        Set-ItemProperty $settings -Name Lang -Value $language[0]
        $gui = Start-Process (Join-Path $InstallDir '7zFM.exe') -ArgumentList ('"' + $TestDir + '"') -PassThru
        try {
            $deadline = (Get-Date).AddSeconds(25)
            do {
                Start-Sleep -Milliseconds 250
                $gui.Refresh()
                $text = [ShellSmoke]::MenuText($gui.MainWindowHandle)
            } until ($text.Contains($language[1]) -or (Get-Date) -gt $deadline -or $gui.HasExited)
            if (-not $text.Contains($language[1])) { throw "GUI language $($language[0]) failed: $text" }
            Write-Output "PASS live File Manager language $($language[0]): $text"
        } finally { if (-not $gui.HasExited) { Stop-Process -Id $gui.Id -Force } }
    }
} finally {
    if ($hadLang) { Set-ItemProperty $settings -Name Lang -Value $oldLang }
    else { Remove-ItemProperty $settings -Name Lang -ErrorAction SilentlyContinue }
}

New-Item -ItemType Directory -Force "$TestDir/input", "$TestDir/extracted" | Out-Null
$inputFile = Join-Path $TestDir 'input/payload.bin'
[IO.File]::WriteAllBytes($inputFile, [Text.Encoding]::UTF8.GetBytes(('LZ4 context menu test ' * 8192)))
$archive = Join-Path $TestDir 'extracted/payload.bin.lz4'
& (Join-Path $InstallDir '7zz.exe') a -tlz4 $archive $inputFile -y | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Cannot create shell test archive' }
[ShellSmoke]::Invoke($archive, 'SevenZipExtractHere')
$output = Join-Path $TestDir 'extracted/payload.bin'
$deadline = (Get-Date).AddSeconds(30)
do { Start-Sleep -Milliseconds 250 } until ((Test-Path $output) -or (Get-Date) -gt $deadline)
if (-not (Test-Path $output)) { throw 'Native context-menu extraction produced no output' }
if ((Get-FileHash $inputFile).Hash -ne (Get-FileHash $output).Hash) { throw 'Context-menu extraction content mismatch' }
Write-Output 'PASS native IContextMenu Extract Here launches 7zG.exe from custom install directory and restores exact content'

[ShellSmoke]::Invoke($inputFile, 'SevenZipCompressToZip')
$deadline = (Get-Date).AddSeconds(30)
$zips = @()
do {
    Start-Sleep -Milliseconds 250
    $zips = @(Get-ChildItem (Split-Path $inputFile) -Filter '*.zip')
} until ($zips.Count -gt 0 -or (Get-Date) -gt $deadline)
if ($zips.Count -ne 1) { throw 'Native context-menu compression produced no ZIP' }
& (Join-Path $InstallDir '7zz.exe') t $zips[0].FullName | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Context-menu ZIP integrity check failed' }
Write-Output 'PASS native IContextMenu Compress to ZIP launches installed 7zG.exe'
