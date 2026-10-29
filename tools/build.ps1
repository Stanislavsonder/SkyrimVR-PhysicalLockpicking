# Builds the plugin, the core tests and the package on Windows, as CI does.
#
#   tools\build.ps1                build, test, package to out\VRLockpicking.zip
#   tools\build.ps1 -Lockplacer    also tools\lockplacer (out-lockplacer\lockplacer.exe)
#
# Needs Visual Studio 2022 (Build Tools will do) with the C++ workload, CMake, Ninja, Python and
# vcpkg (VCPKG_ROOT). The first build compiles CommonLib and takes a long time.
param([switch]$Lockplacer)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

# the MSVC environment (cl, link, the Windows SDK) for x64
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
	$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
	if (-not $vs) { throw 'Visual Studio with the C++ tools not found' }
	Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
	Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}
if (-not $env:VCPKG_ROOT) { throw 'VCPKG_ROOT is not set' }

if (-not (Test-Path extern/CommonLibVR/CMakeLists.txt)) {
	git submodule update --init --recursive
}

function Run([string]$a_exe, [string[]]$a_args)
{
	& $a_exe @a_args
	if ($LASTEXITCODE -ne 0) { throw "$a_exe failed ($LASTEXITCODE)" }
}

Run cmake @('-S', '.', '-B', 'build', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
	"-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake",
	'-DVCPKG_TARGET_TRIPLET=x64-windows-static-md', '-DVRLP_BUILD_PLUGIN=ON')
Run cmake @('--build', 'build', '--target', 'VRLockpicking', 'vrlp_tests')
Run ctest @('--test-dir', 'build', '--output-on-failure')
Run python @('tools/package.py', 'build/VRLockpicking.dll', 'out/VRLockpicking.zip')

if ($Lockplacer) {
	# the nifly commit CI builds with
	if (-not (Test-Path build-nifly)) {
		Run git @('clone', '--quiet', 'https://github.com/ousnius/nifly', 'build-nifly')
	}
	Run git @('-C', 'build-nifly', 'checkout', '--quiet', '6cb15fb5da71527b33cea8ff7913be8790e1c633')
	Run cmake @('-S', 'tools/lockplacer', '-B', 'build-lockplacer', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DNIFLY_DIR=$root/build-nifly", '-DBUILD_TESTING=OFF')
	Run cmake @('--build', 'build-lockplacer')
	New-Item -ItemType Directory -Force out-lockplacer | Out-Null
	Copy-Item build-lockplacer/lockplacer.exe, tools/lockplacer/README.md out-lockplacer/
}
Write-Host "Done: out\VRLockpicking.zip$(if ($Lockplacer) { ', out-lockplacer\lockplacer.exe' })"
