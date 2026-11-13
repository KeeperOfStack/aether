# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

# Builds only what the client and server installers need, on Windows, locally.
# Prerequisites are installed under $ToolRoot by the setup steps in docs/c-dev.
# Usage: scripts\build-windows-local.ps1 [-Clean] [-Targets mumble,mumble-server]

param(
	[string]$ToolRoot = 'D:\aether-toolchain',
	[string[]]$Targets = @('all'),
	[switch]$Clean
)

$ErrorActionPreference = 'Stop'

$repo      = Split-Path -Parent $PSScriptRoot
$buildDir  = Join-Path $ToolRoot 'build\min'
$envDir    = Join-Path $ToolRoot 'build_env'
$cmake     = Join-Path $ToolRoot 'tools\python\cmake\data\bin\cmake.exe'
$ninja     = Join-Path $ToolRoot 'tools\python\bin\ninja.exe'
$ccache    = Get-ChildItem (Join-Path $ToolRoot 'tools\ccache') -Recurse -Filter ccache.exe | Select-Object -First 1 -ExpandProperty FullName
$vcvars    = Join-Path $ToolRoot 'vs2026\VC\Auxiliary\Build\vcvars64.bat'

# Import the MSVC environment into this PowerShell session.
$vcEnv = cmd /c "`"$vcvars`" >nul && set"
foreach ($line in $vcEnv) {
	if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
}
$env:PATH = "$(Join-Path $ToolRoot 'tools\WixSharp');$env:PATH"
$env:WIXSHARP_WIXDIR = Join-Path $ToolRoot 'tools\WixSharp\Wix_bin\bin'
$env:CCACHE_DIR = Join-Path $ToolRoot 'ccache-cache'
$env:CCACHE_MAXSIZE = '20G'

if ($Clean -and (Test-Path $buildDir)) { Remove-Item -Recurse -Force $buildDir }

$total = [System.Diagnostics.Stopwatch]::StartNew()

if (-not (Test-Path (Join-Path $buildDir 'build.ninja'))) {
	$configure = [System.Diagnostics.Stopwatch]::StartNew()
	& $cmake -G Ninja `
		-DCMAKE_MAKE_PROGRAM="$ninja" `
		-DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl `
		-DCMAKE_C_COMPILER_LAUNCHER="$ccache" -DCMAKE_CXX_COMPILER_LAUNCHER="$ccache" `
		-DCMAKE_BUILD_TYPE=Release `
		-DCMAKE_TOOLCHAIN_FILE="$envDir/scripts/buildsystems/vcpkg.cmake" `
		-DVCPKG_TARGET_TRIPLET=x64-windows-static-md `
		-DIce_HOME="$envDir/installed/x64-windows-static-md" `
		-Dstatic=ON -Dpackaging=ON -Dsidecar=ON `
		-Dtests=OFF -Dtranslations=OFF -Doverlay-xcompile=ON `
		-Dlto=OFF -Dsymbols=OFF `
		-Ddatabase-sqlite-tests=OFF -Ddatabase-mysql-tests=OFF -Ddatabase-postgresql-tests=OFF `
		-DBUILD_NUMBER=0 `
		-S $repo -B $buildDir
	if ($LASTEXITCODE -ne 0) { throw "configure failed" }
	Write-Host ("configure: {0:N0} s" -f $configure.Elapsed.TotalSeconds)
}

$build = [System.Diagnostics.Stopwatch]::StartNew()
& $cmake --build $buildDir --config Release --target @Targets
if ($LASTEXITCODE -ne 0) { throw "build failed" }
Write-Host ("build: {0:N0} s" -f $build.Elapsed.TotalSeconds)

Write-Host ("total: {0:N0} s" -f $total.Elapsed.TotalSeconds)
Get-ChildItem $buildDir -Recurse -Filter 'mumble_*-1.7.0-x64.msi' | ForEach-Object { Write-Host "installer: $($_.FullName)" }
