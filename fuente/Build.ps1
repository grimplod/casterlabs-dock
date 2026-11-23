param(
    [Parameter(Mandatory)][string]$Zig,
    [Parameter(Mandatory)][string]$WinSysroot,
    [Parameter(Mandatory)][string]$Qt,
    [Parameter(Mandatory)][string]$ObsSource,
    [Parameter(Mandatory)][string]$BuildDirectory
)

$ErrorActionPreference = 'Stop'
$Zig = (Resolve-Path -LiteralPath $Zig).Path
$WinSysroot = (Resolve-Path -LiteralPath $WinSysroot).Path
$Qt = (Resolve-Path -LiteralPath $Qt).Path
$ObsSource = (Resolve-Path -LiteralPath $ObsSource).Path
$build = (New-Item -ItemType Directory -Path $BuildDirectory -Force).FullName

function Invoke-Zig {
    $previousZigLibc = $env:ZIG_LIBC
    try {
        if ($libc) { $env:ZIG_LIBC = $libc }
        & $Zig @args
        if ($LASTEXITCODE -ne 0) { throw "Zig terminó con código $LASTEXITCODE." }
    } finally {
        $env:ZIG_LIBC = $previousZigLibc
    }
}

$tools = Get-ChildItem -LiteralPath (Join-Path $WinSysroot 'VC/Tools/MSVC') -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$sdk = Join-Path $WinSysroot 'Windows Kits/10'
$sdkVersion = Get-ChildItem -LiteralPath (Join-Path $sdk 'Include') -Directory |
    Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty Name
$sdkIncludes = Join-Path $sdk "Include/$sdkVersion"
$sdkLibraries = Join-Path $sdk "Lib/$sdkVersion"

# These configure-time values describe the stable OBS headers used by this module.
@('#pragma once', '#define OBS_RELEASE_CANDIDATE 0', '#define OBS_BETA 0') |
    Set-Content -LiteralPath (Join-Path $build 'obsconfig.h') -Encoding ascii

$imports = @{
    'obs' = @('blog')
    'obs-frontend-api' = @('obs_frontend_add_dock_by_id', 'obs_frontend_add_event_callback')
}
foreach ($module in $imports.Keys) {
    $definition = Join-Path $build "$module.def"
    @("LIBRARY $module.dll", 'EXPORTS') + $imports[$module] |
        Set-Content -LiteralPath $definition -Encoding ascii
    Invoke-Zig dlltool -m i386:x86-64 -d $definition -l (Join-Path $build "$module.lib")
}

$libc = Join-Path $build 'zig-libc.conf'
$libcConfig = @(
    "include_dir=$sdkIncludes/ucrt"
    "sys_include_dir=$($tools.FullName)/include"
    "crt_dir=$sdkLibraries/ucrt/x64"
    "msvc_lib_dir=$($tools.FullName)/lib/x64"
    "kernel32_lib_dir=$sdkLibraries/um/x64"
    'gcc_dir='
)
[IO.File]::WriteAllText($libc, ($libcConfig -join "`n") + "`n")

$compiler = @('cc', '-target', 'x86_64-windows-msvc', '-fms-compatibility-version=19.44')
$object = Join-Path $build 'casterlabs-dock.obj'
Invoke-Zig @compiler -x c++ -std=c++20 -O2 -Wall -Wextra -Wno-ignored-attributes -D_DLL -D_MT `
    "-ffile-prefix-map=$Qt=qt" "-ffile-prefix-map=$ObsSource=obs" `
    "-ffile-prefix-map=$WinSysroot=windows-sdk" "-ffile-prefix-map=$PSScriptRoot=fuente" `
    -isystem (Join-Path $tools.FullName 'include') `
    -isystem (Join-Path $sdkIncludes 'ucrt') -isystem (Join-Path $sdkIncludes 'shared') `
    -isystem (Join-Path $sdkIncludes 'um') `
    -isystem (Join-Path $Qt 'include') -isystem (Join-Path $Qt 'include/QtCore') `
    -isystem (Join-Path $Qt 'include/QtGui') -isystem (Join-Path $Qt 'include/QtWidgets') `
    -isystem (Join-Path $ObsSource 'libobs') -isystem (Join-Path $ObsSource 'frontend/api') `
    -I $build -c (Join-Path $PSScriptRoot 'casterlabs-dock.cpp') -o $object

Invoke-Zig @compiler -shared -nostdlib -o (Join-Path $build 'casterlabs-dock.dll') $object `
    -L (Join-Path $tools.FullName 'lib/x64') -L (Join-Path $sdkLibraries 'ucrt/x64') `
    -L (Join-Path $sdkLibraries 'um/x64') -L (Join-Path $Qt 'lib') -L $build `
    -lobs -lobs-frontend-api -lQt6Widgets -lQt6Gui -lQt6Core `
    -luser32 -lkernel32 -lmsvcrt -lmsvcprt -lvcruntime -lucrt -loldnames
