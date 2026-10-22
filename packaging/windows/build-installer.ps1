param(
    [Parameter(Mandatory)][string]$PackageDir,
    [Parameter(Mandatory)][string]$DistDir,
    [string]$Version = (Get-Content "$PSScriptRoot/../version.txt" -Raw).Trim()
)
$ErrorActionPreference = 'Stop'
$tempDir = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [IO.Path]::GetTempPath() }
$compilerDir = Join-Path $tempDir 'inno-6.7.0'
$compiler = Join-Path $compilerDir 'ISCC.exe'
if (-not (Test-Path $compiler)) {
    $download = Join-Path ([IO.Path]::GetTempPath()) 'innosetup-6.7.0.exe'
    Invoke-WebRequest 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_0/innosetup-6.7.0.exe' -OutFile $download
    if ((Get-FileHash $download -Algorithm SHA256).Hash -ne 'F45C7D68D1E660CF13877EC36738A5179CE72A33414F9959D35E99B68C52A697') {
        throw 'Inno Setup download checksum mismatch'
    }
    $result = Start-Process $download -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', ('/DIR="' + $compilerDir + '"') -Wait -PassThru
    if ($result.ExitCode -ne 0) { throw "Inno Setup provisioning failed: $($result.ExitCode)" }
}
& $compiler "/DAppVersion=$Version" "/DPackageDir=$((Resolve-Path $PackageDir).Path)" "/DDistDir=$((Resolve-Path $DistDir).Path)" "$PSScriptRoot/7zip-lz4.iss"
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed' }
