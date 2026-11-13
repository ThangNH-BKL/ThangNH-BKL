param([string]$ToolRoot='', [switch]$InstallTools, [switch]$Tls)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$ToolRoot){$ToolRoot=Join-Path $root '.test-tools'}
$ToolRoot=[IO.Path]::GetFullPath($ToolRoot)
$build=Join-Path $root 'build-zig'
New-Item -ItemType Directory -Force -Path $build | Out-Null
if($InstallTools){
 python -m pip install --cache-dir (Join-Path $root '.test-cache') --target $ToolRoot ziglang==0.13.0 cmake==3.31.6 ninja==1.11.1.4 -r (Join-Path $PSScriptRoot 'requirements.txt')
 if($LASTEXITCODE){throw 'Development tool install failed'}
 if($Tls){python -m pip install --cache-dir (Join-Path $root '.test-cache') --target $ToolRoot -r (Join-Path $PSScriptRoot 'requirements-tls.txt');if($LASTEXITCODE){throw 'TLS test dependencies failed'}}
}
$zig=Join-Path $ToolRoot 'ziglang/zig.exe'
$cmake=Join-Path $ToolRoot 'cmake/data/bin/cmake.exe'
$ctest=Join-Path $ToolRoot 'cmake/data/bin/ctest.exe'
$ninja=Join-Path $ToolRoot 'bin/ninja.exe'
foreach($binary in @($zig,$cmake,$ninja)){if(!(Test-Path -LiteralPath $binary)){throw "Missing $binary; rerun with -InstallTools"}}
foreach($entry in @(@('cc','cc'),@('cxx','c++'),@('ar','ar'),@('ranlib','ranlib'))){
 $command="@echo off`r`n`"$zig`" $($entry[1]) %*`r`n"
 Set-Content -LiteralPath (Join-Path $build "$($entry[0]).cmd") -Value $command -Encoding ASCII
}
$buildForward=$build.Replace('\','/')
$project="set(CMAKE_AR `"$buildForward/ar.cmd`")`nset(CMAKE_RANLIB `"$buildForward/ranlib.cmd`")`n"
$project+='set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> qc <TARGET> <LINK_FLAGS> <OBJECTS>")' + "`n"
$project+='set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> qc <TARGET> <LINK_FLAGS> <OBJECTS>")' + "`n"
$project+='set(CMAKE_CXX_ARCHIVE_FINISH "<CMAKE_RANLIB> <TARGET>")' + "`n"
$project+='set(CMAKE_C_ARCHIVE_FINISH "<CMAKE_RANLIB> <TARGET>")' + "`n"
$project+='set(CMAKE_SIZEOF_VOID_P 8)' + "`n"
Set-Content -LiteralPath (Join-Path $build 'project.cmake') -Value $project -Encoding ASCII
$env:PYTHONPATH=$ToolRoot
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $build 'zig-cache'
$env:ZIG_LOCAL_CACHE_DIR=Join-Path $build 'zig-local-cache'
& $cmake -S $root -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_C_COMPILER=$build/cc.cmd" "-DCMAKE_CXX_COMPILER=$build/cxx.cmd" "-DCMAKE_PROJECT_INCLUDE=$build/project.cmake" -DCMAKE_BUILD_TYPE=Release -DCAMERA_BUILD_GUI=OFF -DCAMERA_BUILD_CURL=ON -DBUILD_TESTING=ON
if($LASTEXITCODE){throw 'Core configure failed'}
& $cmake --build $build --parallel 4
if($LASTEXITCODE){throw 'Core build failed'}
& $ctest --test-dir $build --output-on-failure
if($LASTEXITCODE){throw 'Core tests failed'}
& (Join-Path $build 'CameraResourceTests.exe')
if($LASTEXITCODE){throw 'Resource tests failed'}
python (Join-Path $PSScriptRoot 'run_ftp.py') (Join-Path $build 'CameraFtpIntegration.exe')
if($LASTEXITCODE){throw 'FTP integration failed'}
if($Tls){python (Join-Path $PSScriptRoot 'run_ftps.py') (Join-Path $build 'CameraFtpIntegration.exe');if($LASTEXITCODE){throw 'FTPS integration failed'}}
python (Join-Path $PSScriptRoot 'audit.py')
if($LASTEXITCODE){throw 'Dependency audit failed'}
