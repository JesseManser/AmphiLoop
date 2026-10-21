param([string]$JuceSource = '', [switch]$PackageOnly)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot 'build'
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmakeExe = if ($cmakeCommand) { $cmakeCommand.Source } else { 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' }
if (-not (Test-Path -LiteralPath $cmakeExe)) { throw 'Install CMake 3.22+ and Visual Studio 2022 C++ build tools.' }
$ctestExe = Join-Path (Split-Path $cmakeExe) 'ctest.exe'
if (-not $PackageOnly) {
    $configureArgs = @('-S',$projectRoot,'-B',$buildRoot,'-G','Visual Studio 17 2022','-A','x64')
    if ($JuceSource) { $configureArgs += "-DFETCHCONTENT_SOURCE_DIR_JUCE=$JuceSource" }
    & $cmakeExe @configureArgs
    if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
    & $cmakeExe --build $buildRoot --config Release --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    & $ctestExe --test-dir $buildRoot -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
}
$bundle = Join-Path $buildRoot 'AmphiLoop_artefacts\Release\VST3\AmphiLoop.vst3'
if (-not (Test-Path -LiteralPath (Join-Path $bundle 'Contents\x86_64-win\AmphiLoop.vst3'))) { throw 'Release VST3 missing' }
$package = Join-Path $projectRoot 'dist\AmphiLoop-0.3.3-alpha-Windows-x64'
if ((Test-Path -LiteralPath $package) -or (Test-Path -LiteralPath ($package+'.zip'))) { throw 'Package exists; preserve it or choose a new version before repackaging.' }
New-Item -ItemType Directory -Path $package -Force | Out-Null
Copy-Item -LiteralPath $bundle -Destination $package -Recurse
# Source files included in the release.
$files = @('Source/LoopEngine.h','Source/LoopEngine.cpp','Source/PluginProcessor.h','Source/PluginProcessor.cpp',
    'Source/TelemetryParameter.h','Source/PluginEditor.h','Source/PluginEditor.cpp','Tests/EngineTests.cpp','Tests/ProcessorTests.cpp','Tests/HostTests.cpp',
    'docs/preview.png','Assets/AmphiLoopWordmark.svg',
    'scripts/Build-Release.ps1','.clang-format','.gitignore','CMakeLists.txt','README.md','LICENSE','THIRD_PARTY_NOTICES.md')
foreach ($name in $files) {
    $destination = Join-Path $package $name
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $destination
}
Get-ChildItem -LiteralPath $package -Recurse -File | Sort-Object FullName | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(),$_.FullName.Substring($package.Length+1)
} | Set-Content -LiteralPath (Join-Path $package 'SHA256SUMS.txt') -Encoding utf8
Compress-Archive -LiteralPath $package -DestinationPath ($package+'.zip') -CompressionLevel Optimal
Get-FileHash -LiteralPath ($package+'.zip') -Algorithm SHA256


