# Stages the RXDK toolchain into an RXDK root, the same layout the RXDK VS Code / Visual
# Studio setup installs:
#   llvm/   RXDK clang          (Team-Resurgent/llvm-project, latest)
#   sdk/    headers + libs      (Team-Resurgent/RXDK-SDK, main)
#   tools/  Rxdk.Cli, imagebld, ... (Team-Resurgent/RXDK-Tools, latest)
# The default root is RXDK's own: Windows %ProgramData%\RXDK, macOS
# ~/Library/Application Support/RXDK, Linux $XDG_DATA_HOME/rxdk (~/.local/share/rxdk);
# the RXDK environment variable overrides it (build.sh reads the same variable).
# Runs in PowerShell 7 (pwsh) on Windows, Linux and macOS, and in Windows PowerShell 5.1.
# Used by CI (adapted from R3muxTool's script, without xdvdfs: no ISO is built here).
param(
    [string]$Root = ''
)
$ErrorActionPreference = 'Stop'

$onWindows = ($PSVersionTable.PSEdition -eq 'Desktop') -or $IsWindows
$arch = if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq 'Arm64') { 'arm64' } else { 'x64' }
if ($onWindows) {
    $os = 'windows'; $rid = "win-$arch"; $exe = '.exe'
    $default = Join-Path $(if ($env:ProgramData) { $env:ProgramData } else { 'C:\ProgramData' }) 'RXDK'
} elseif ($IsMacOS) {
    $os = 'macos'; $rid = "osx-$arch"; $exe = ''
    $default = Join-Path $HOME 'Library/Application Support/RXDK'
} else {
    $os = 'linux'; $rid = "linux-$arch"; $exe = ''
    $default = Join-Path $(if ($env:XDG_DATA_HOME) { $env:XDG_DATA_HOME } else { Join-Path $HOME '.local/share' }) 'rxdk'
}
if (-not $Root) { $Root = if ($env:RXDK) { $env:RXDK } else { $default } }

$tmp = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [IO.Path]::GetTempPath() }
$llvm = Join-Path $Root 'llvm'
$sdk = Join-Path $Root 'sdk'
$tools = Join-Path $Root 'tools'
New-Item -ItemType Directory -Force -Path $llvm, $tools | Out-Null

function Fetch([string]$url, [string]$name) {
    $zip = Join-Path $tmp $name
    Write-Host "downloading $url"
    # curl.exe on Windows (in Windows PowerShell 5.1 plain "curl" is Invoke-WebRequest)
    & $(if ($onWindows) { 'curl.exe' } else { 'curl' }) -fsSL -o $zip $url
    if ($LASTEXITCODE -ne 0) { throw "download failed: $url" }
    return $zip
}

function Expand([string]$zip, [string]$dest) {
    if ($onWindows) { tar.exe -xf $zip -C $dest } else { unzip -q -o $zip -d $dest }
    if ($LASTEXITCODE -ne 0) { throw "extract failed: $zip" }
}

# RXDK clang
$clang = Join-Path $llvm "xbox-$os-$arch/bin/clang$exe"
if (-not (Test-Path $clang)) {
    $zip = Fetch "https://github.com/Team-Resurgent/llvm-project/releases/download/latest/xbox-$os-$arch.zip" "xbox-$os-$arch.zip"
    Expand $zip $llvm
    if (-not (Test-Path $clang)) { throw "clang missing after extract: $clang" }
    if (-not $onWindows) { chmod -R +x (Join-Path $llvm "xbox-$os-$arch/bin") }
}

# SDK
if (-not (Test-Path (Join-Path $sdk 'include'))) {
    # git reports progress on stderr; Windows PowerShell 5.1 would treat that as an error.
    $ErrorActionPreference = 'Continue'
    git clone --quiet --depth 1 https://github.com/Team-Resurgent/RXDK-SDK.git $sdk 2>&1 | Out-Host
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -ne 0) { throw 'cloning RXDK-SDK failed' }
}
if (-not (Test-Path (Join-Path $sdk 'lib/libxapi.lib'))) { throw 'RXDK-SDK libxapi.lib missing' }

# Host tools (Rxdk.Cli, imagebld, ...)
$zip = Fetch "https://github.com/Team-Resurgent/RXDK-Tools/releases/download/latest/rxdk-managed-$rid.zip" 'rxdk-tools.zip'
$extract = Join-Path $tmp 'rxdk-tools'
Remove-Item -Recurse -Force $extract -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $extract | Out-Null
Expand $zip $extract
$toolsDir = Get-ChildItem $extract -Recurse -Directory -Filter tools | Select-Object -First 1
if (-not $toolsDir) { throw 'tools/ missing from the RXDK-Tools release' }
Copy-Item (Join-Path $toolsDir.FullName '*') $tools -Recurse -Force
foreach ($t in 'Rxdk.Cli', 'imagebld') {
    $p = Join-Path $tools "$t$exe"
    if (-not (Test-Path $p)) { throw "$t$exe missing from the RXDK-Tools release" }
    if (-not $onWindows) { chmod +x $p }
}

Write-Host "RXDK staged at $Root"
