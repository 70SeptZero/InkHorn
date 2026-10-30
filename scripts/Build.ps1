[CmdletBinding()]
param(
    [ValidateSet('release','debug','ui-preview')][string]$Preset = 'release',
    [string]$CMake = '',
    [string]$Ninja = '',
    [string]$BuildDirectory = '',
    [string[]]$CMakeOptions = @(),
    [switch]$Clean,
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
$projectDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Find-JetBrainsTool([string]$name,[string]$relativePath) {
    $command = Get-Command $name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $roots = @((Join-Path $env:ProgramFiles 'JetBrains'),'D:/Application/JetBrains')
    if ($env:CLION_HOME) { $candidate = Join-Path $env:CLION_HOME $relativePath; if (Test-Path -LiteralPath $candidate) { return $candidate } }
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        foreach ($installation in (Get-ChildItem -LiteralPath $root -Directory -Filter '*CLion*' | Sort-Object Name -Descending)) {
            $candidate = Join-Path $installation.FullName $relativePath
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }
    throw "没有找到 $name。可用 -CMake / -Ninja 指定 CLion 内置工具的完整路径，或设置 CLION_HOME。"
}
if (-not $CMake) { $CMake = Find-JetBrainsTool 'cmake' 'bin/cmake/win/x64/bin/cmake.exe' }
if (-not $Ninja) { $Ninja = Find-JetBrainsTool 'ninja' 'bin/ninja/win/x64/ninja.exe' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw '未找到 Visual Studio C++ 工具链。请安装后再继续。' }
$devCmd = Join-Path $vsPath 'Common7/Tools/VsDevCmd.bat'
$environmentProcess = [Diagnostics.Process]::new()
$environmentProcess.StartInfo.FileName = $env:ComSpec
$environmentProcess.StartInfo.Arguments = '/d /s /c ""' + $devCmd + '" -no_logo -arch=amd64 -host_arch=amd64 >nul && set"'
$environmentProcess.StartInfo.UseShellExecute = $false
$environmentProcess.StartInfo.CreateNoWindow = $true
$environmentProcess.StartInfo.RedirectStandardOutput = $true
$environmentProcess.StartInfo.RedirectStandardError = $true
$null = $environmentProcess.Start()
$environmentDump = $environmentProcess.StandardOutput.ReadToEnd() -split "`r?`n"
$environmentError = $environmentProcess.StandardError.ReadToEnd()
$environmentProcess.WaitForExit()
if ($environmentProcess.ExitCode -ne 0) { throw "Visual Studio amd64 开发环境初始化失败：$environmentError" }
foreach ($entry in $environmentDump) {
    $split = $entry.IndexOf('=')
    if ($split -gt 0) { [Environment]::SetEnvironmentVariable($entry.Substring(0,$split),$entry.Substring($split+1),'Process') }
}
$env:VSLANG = '1033'
Push-Location -LiteralPath $projectDirectory
try {
    if (-not $BuildDirectory) { $BuildDirectory = Join-Path $projectDirectory $(if ($Preset -eq 'ui-preview') {'build/msvc-ui-preview'} elseif ($Preset -eq 'debug') {'build/clion-debug'} else {"build/$Preset"}) }
    & $CMake --preset $Preset "-B$BuildDirectory" "-DCMAKE_MAKE_PROGRAM=$Ninja" @CMakeOptions
    if ($LASTEXITCODE -ne 0) { throw 'CMake 配置失败。请按错误提示准备 PDFium，或选择 ui-preview。' }
    $buildOptions = @('--build',$BuildDirectory,'--parallel','4')
    if ($Clean) { $buildOptions += '--clean-first' }
    & $CMake @buildOptions
    if ($LASTEXITCODE -ne 0) { throw '编译失败。' }
    if ($Test) {
        $ctest = Join-Path (Split-Path -Parent $CMake) 'ctest.exe'
        & $ctest --test-dir $BuildDirectory --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw '测试未通过。' }
    }
} finally { Pop-Location }
