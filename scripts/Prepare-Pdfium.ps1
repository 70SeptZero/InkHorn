[CmdletBinding()]
param(
    [string]$DependencyDirectory = '',
    [string]$Proxy = '',
    [ValidateRange(1,64)][int]$Jobs = 4,
    [switch]$SkipSync,
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
$projectDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$lock = Get-Content -LiteralPath (Join-Path $projectDirectory 'dependencies.lock.json') -Raw | ConvertFrom-Json
if (-not $DependencyDirectory) { $DependencyDirectory = Join-Path $projectDirectory 'third_party/source' }
$dependencyRoot = [IO.Path]::GetFullPath($DependencyDirectory)
if ($dependencyRoot -match '\s') { throw 'PDFium/Chromium 源码目录不能含空格。请用 -DependencyDirectory 指定其他目录。' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw '请先安装 Visual Studio Build Tools 的 C++ 桌面开发工具和 Windows SDK。' }
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw '未发现 MSVC C++ x64 工具链。CLion 自带 MinGW 无法替代此源码构建所需工具。' }
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdkInclude = Join-Path $sdkRoot 'Include/10.0.28000.0/um/Windows.h'
if (-not (Test-Path -LiteralPath $sdkInclude)) { throw '锁定的 PDFium 构建需要 Windows SDK 10.0.28000.0（安装包 10.0.28000.2270 或对应更新）。请在 VS Installer 中安装。' }
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw '请安装 Git for Windows 并重启 CLion 终端。' }
Write-Host "Visual Studio: $vsPath"
Write-Host "Windows SDK: $sdkRoot"
Write-Host "PDFium commit: $($lock.pdfium.revision)"
if ($CheckOnly) { Write-Host '基础依赖检查通过。'; exit 0 }

if (-not $Proxy) {
    $networkSettings = Get-ItemProperty -LiteralPath 'HKCU:/Software/Microsoft/Windows/CurrentVersion/Internet Settings' -ErrorAction SilentlyContinue
    if ($networkSettings.ProxyEnable -eq 1 -and [string]$networkSettings.ProxyServer -match '^https?://[^@\s;=]+$') {
        $Proxy = [string]$networkSettings.ProxyServer
    }
}

function Invoke-Checked([string]$Program,[string[]]$Arguments) {
    if ($Program -eq 'git') {
        $Arguments = @('-c','core.longpaths=true','-c','core.autocrlf=false') + $Arguments
        if ($Proxy) { $Arguments = @('-c',"http.proxy=$Proxy") + $Arguments }
    }
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program 执行失败（$LASTEXITCODE）。修复网络/依赖后可重跑脚本。" }
}
$depot = Join-Path $dependencyRoot 'depot_tools'
$checkout = Join-Path $dependencyRoot 'checkout'
$source = Join-Path $checkout 'pdfium'
$stage = Join-Path $projectDirectory 'third_party/pdfium'
New-Item -ItemType Directory -Force -Path $dependencyRoot,$checkout,$stage,(Join-Path $stage 'include'),(Join-Path $stage 'lib') | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $depot '.git'))) {
    Invoke-Checked 'git' @('clone','--filter=blob:none','--no-checkout',$lock.depot_tools.repository,$depot)
}
Invoke-Checked 'git' @('-C',$depot,'fetch','--depth=1','origin',$lock.depot_tools.revision)
Invoke-Checked 'git' @('-C',$depot,'checkout','--detach',$lock.depot_tools.revision)
$spec = @"
solutions = [{
  'name': 'pdfium',
  'url': '$($lock.pdfium.repository)@$($lock.pdfium.revision)',
  'deps_file': 'DEPS',
  'managed': False,
  'custom_vars': {'checkout_configuration': 'minimal', 'checkout_rust': False},
  'custom_deps': {'pdfium/third_party/rust-toolchain': None, 'third_party/rust-toolchain': None}
}]
target_os = []
"@
$gclientFile = Join-Path $checkout '.gclient'
if (Test-Path -LiteralPath $gclientFile) {
    $existingSpec = Get-Content -LiteralPath $gclientFile -Raw
    if ($existingSpec -notmatch [regex]::Escape($lock.pdfium.repository)) { throw '目标目录已有其他 gclient 配置。请指定一个新的依赖目录，脚本不会覆盖。' }
}
[IO.File]::WriteAllText($gclientFile,$spec,[Text.UTF8Encoding]::new($false))
$previousPath = $env:Path
$previousToolchain = $env:DEPOT_TOOLS_WIN_TOOLCHAIN
$previousUpdate = $env:DEPOT_TOOLS_UPDATE
$previousVs2026 = $env:vs2026_install
$previousVs2022 = $env:vs2022_install
$previousHttpProxy = $env:HTTP_PROXY
$previousHttpsProxy = $env:HTTPS_PROXY
$localEnvironmentNames = @('VPYTHON_VIRTUALENV_ROOT','CIPD_CACHE_DIR','UV_CACHE_DIR','BOTO_CONFIG','TEMP','TMP','GIT_CONFIG_COUNT','GIT_CONFIG_KEY_0','GIT_CONFIG_VALUE_0','GIT_CONFIG_KEY_1','GIT_CONFIG_VALUE_1')
$previousLocalEnvironment = @{}
foreach ($name in $localEnvironmentNames) { $previousLocalEnvironment[$name] = [Environment]::GetEnvironmentVariable($name,'Process') }
try {
    $env:Path = "$depot;$env:Path"
    $env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
    $env:DEPOT_TOOLS_UPDATE = '0'
    $env:GIT_CONFIG_COUNT = '2'
    $env:GIT_CONFIG_KEY_0 = 'core.longpaths'
    $env:GIT_CONFIG_VALUE_0 = 'true'
    $env:GIT_CONFIG_KEY_1 = 'core.autocrlf'
    $env:GIT_CONFIG_VALUE_1 = 'false'
    if ($Proxy) { $env:HTTP_PROXY = $Proxy; $env:HTTPS_PROXY = $Proxy }
    $buildCache = Join-Path $dependencyRoot 'cache'
    foreach ($folder in @('vpython','cipd','uv','temp','gsutil')) { New-Item -ItemType Directory -Force -Path (Join-Path $buildCache $folder) | Out-Null }
    $env:VPYTHON_VIRTUALENV_ROOT = Join-Path $buildCache 'vpython'
    $env:CIPD_CACHE_DIR = Join-Path $buildCache 'cipd'
    $env:UV_CACHE_DIR = Join-Path $buildCache 'uv'
    $env:TEMP = Join-Path $buildCache 'temp'
    $env:TMP = $env:TEMP
    $env:BOTO_CONFIG = Join-Path $buildCache 'public-downloads.boto'
    $boto = "[GSUtil]`nstate_dir = " + (Join-Path $buildCache 'gsutil') + "`n"
    [IO.File]::WriteAllText($env:BOTO_CONFIG,$boto,[Text.UTF8Encoding]::new($false))
    if ($vsPath -match '2022') { $env:vs2022_install = $vsPath } else { $env:vs2026_install = $vsPath }
    Push-Location -LiteralPath $checkout
    try { if (-not $SkipSync) { Invoke-Checked (Join-Path $depot 'gclient.bat') @('sync','--no-history','--shallow') } } finally { Pop-Location }
    $argsGn = @'
is_debug = false
is_component_build = false
target_cpu = "x64"
pdf_is_complete_lib = true
pdf_is_standalone = false
pdf_enable_v8 = false
pdf_enable_xfa = false
pdf_use_skia = false
pdf_enable_fontations = false
pdf_enable_rust_bmp = false
pdf_enable_rust_jpeg = false
pdf_enable_rust_png = false
pdf_use_partition_alloc = false
use_custom_libcxx = false
icu_use_data_file = false
enable_rust = false
enable_rust_cxx = false
is_clang = true
clang_use_chrome_plugins = false
use_remoteexec = false
use_siso = false
symbol_level = 0
treat_warnings_as_errors = false
'@
    $out = Join-Path $source 'out/InkHorn'
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    [IO.File]::WriteAllText((Join-Path $out 'args.gn'),$argsGn,[Text.UTF8Encoding]::new($false))
    Push-Location -LiteralPath $source
    try {
        Invoke-Checked (Join-Path $source 'buildtools/win/gn.exe') @('gen','out/InkHorn','--fail-on-unused-args')
        Invoke-Checked (Join-Path $source 'third_party/ninja/ninja.exe') @('-C','out/InkHorn','pdfium','-j',"$Jobs")
    } finally { Pop-Location }
    $library = Join-Path $out 'obj/pdfium.lib'
    if (-not (Test-Path -LiteralPath $library)) { throw "未找到完整静态库：$library。请检查 GN 构建输出。" }
    Copy-Item -LiteralPath $library -Destination (Join-Path $stage 'lib/pdfium.lib') -Force
    Get-ChildItem -LiteralPath (Join-Path $source 'public') -Filter '*.h' -File | Copy-Item -Destination (Join-Path $stage 'include') -Force
    Copy-Item -LiteralPath (Join-Path $source 'LICENSE') -Destination (Join-Path $stage 'PDFIUM-LICENSE.txt') -Force
    $notices = [Text.StringBuilder]::new()
    $null = $notices.AppendLine("InkHorn third-party notices`nPDFium revision: $($lock.pdfium.revision)`n")
    $null = $notices.AppendLine([IO.File]::ReadAllText((Join-Path $source 'LICENSE')))
    $licenseFiles = Get-ChildItem -LiteralPath (Join-Path $source 'third_party') -Recurse -File |
        Where-Object { $_.Name -match '^(LICENSE([._-].*)?|COPYING([._-].*)?|FTL\.TXT|README\.ijg)$' -and $_.FullName -notmatch '[\\/]\.git[\\/]' -and $_.Length -lt 1000000 }
    foreach ($license in $licenseFiles) {
        $null = $notices.AppendLine("`n===== $($license.FullName.Substring($source.Length + 1)) =====")
        $null = $notices.AppendLine([IO.File]::ReadAllText($license.FullName))
    }
    [IO.File]::WriteAllText((Join-Path $stage 'THIRD_PARTY_NOTICES.txt'),$notices.ToString(),[Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath (Join-Path $projectDirectory 'dependencies.lock.json') -Destination $stage -Force
    Write-Host "静态 PDFium 已准备：$stage"
    Write-Host '现在在 CLion 中选 Visual Studio x64 工具链，加载 release preset 并运行 InkHorn。'
} finally {
    $env:Path = $previousPath
    $env:DEPOT_TOOLS_WIN_TOOLCHAIN = $previousToolchain
    $env:DEPOT_TOOLS_UPDATE = $previousUpdate
    $env:vs2026_install = $previousVs2026
    $env:vs2022_install = $previousVs2022
    $env:HTTP_PROXY = $previousHttpProxy
    $env:HTTPS_PROXY = $previousHttpsProxy
    foreach ($name in $localEnvironmentNames) { [Environment]::SetEnvironmentVariable($name,$previousLocalEnvironment[$name],'Process') }
}
