param(
 [ValidateSet('Debug','Release')][string]$Configuration='Release',
 [switch]$Rebuild,
 [switch]$Package,
 [switch]$Clean
)
$ErrorActionPreference='Stop'

function Find-CMake {
 $command=Get-Command cmake.exe -ErrorAction SilentlyContinue
 if($command){return $command.Source}
 # Visual Studio's bundled CMake is not necessarily on the NMake project's PATH.
 $installations=@()
 if($env:VSINSTALLDIR){$installations+= $env:VSINSTALLDIR}
 $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
 if(Test-Path -LiteralPath $vswhere){
  $installations+= @(& $vswhere -products '*' -version '[17.0,18.0)' -property installationPath)
 }
 foreach($installation in $installations){
  $candidate=Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
  if(Test-Path -LiteralPath $candidate){return $candidate}
 }
 throw 'Cannot find CMake. Visual Studio Installer > Modify > Desktop development with C++ > C++ CMake tools for Windows; then restart Visual Studio. Alternatively install CMake 3.24+ with PATH enabled.'
}
function Invoke-BuildStep {
 param([string]$Title,[string]$Executable,[string[]]$Arguments)
 Write-Host "`n=== $Title ==="
 & $Executable @Arguments
 if($LASTEXITCODE -ne 0){throw "$Title failed (exit $LASTEXITCODE). Read the first error above this message."}
}
$logDirectory=Join-Path $PSScriptRoot 'build-logs'
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
$logPath=Join-Path $logDirectory ("build-{0}-{1}.txt" -f $Configuration,(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$transcriptStarted=$false
$failed=$false
Push-Location $PSScriptRoot
try {
 Start-Transcript -LiteralPath $logPath -Force | Out-Null
 $transcriptStarted=$true
 $cmake=Find-CMake
 $ctest=Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
 if(!(Test-Path -LiteralPath $ctest)){throw "Cannot find ctest.exe next to CMake: $cmake. Repair the CMake installation."}
 Write-Host "Source: $PSScriptRoot"
 Write-Host "Configuration: $Configuration | x64"
 Write-Host "CMake: $cmake"
 Write-Host "Build log: $logPath"
 $build=Join-Path $PSScriptRoot 'build-vs'
 if($Clean){
  if(Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt')){
   Invoke-BuildStep -Title 'Clean' -Executable $cmake -Arguments @('--build',$build,'--config',$Configuration,'--target','clean')
  }else{Write-Host 'Nothing configured yet; clean skipped.'}
 }else{
  Invoke-BuildStep -Title 'Configure (VS2022, v143, x64)' -Executable $cmake -Arguments @('-S',$PSScriptRoot,'-B',$build,'-G','Visual Studio 17 2022','-A','x64','-DCAMERA_BUILD_GUI=ON','-DCAMERA_BUILD_CURL=ON','-DBUILD_TESTING=ON')
  if($Rebuild){Invoke-BuildStep -Title 'Clean' -Executable $cmake -Arguments @('--build',$build,'--config',$Configuration,'--target','clean')}
  Invoke-BuildStep -Title 'Compile and link' -Executable $cmake -Arguments @('--build',$build,'--config',$Configuration,'--parallel','4')
  Invoke-BuildStep -Title 'Automated tests' -Executable $ctest -Arguments @('--test-dir',$build,'-C',$Configuration,'--output-on-failure')
  if($Package){Invoke-BuildStep -Title 'Package' -Executable $cmake -Arguments @('--build',$build,'--config',$Configuration,'--target','package')}
 }
}catch{
 $failed=$true
 Write-Host "`nBUILD FAILED: $($_.Exception.Message)" -ForegroundColor Red
 Write-Host 'Required VS2022 components: Desktop development with C++, MSVC v143 x64/x86, Windows SDK, and C++ MFC for latest v143 build tools (x86 & x64).'
 Write-Host "Full log: $logPath"
}finally{
 if($transcriptStarted){Stop-Transcript | Out-Null}
 Pop-Location
}
if($failed){exit 1}
exit 0
