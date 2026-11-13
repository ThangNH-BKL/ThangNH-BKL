$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$tokens=$null;$errors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile((Join-Path $root 'build.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Build script syntax error'}
$function=$ast.FindAll({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Invoke-BuildStep'},$true)
if($function.Count -ne 1){throw 'Build-step function not found'}
. ([ScriptBlock]::Create($function[0].Extent.Text))
$dir=Join-Path $root 'build-logs/wrapper-selftest'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$native=Join-Path $dir 'native-failure.cmd'
$log=Join-Path $dir 'capture.txt'
Set-Content -LiteralPath $native -Encoding ASCII -Value "@echo off`r`necho NATIVE_STDOUT_MARKER`r`necho NATIVE_STDERR_MARKER 1>&2`r`nexit /b 7"
$expectedFailure=$false
Start-Transcript -LiteralPath $log -Force | Out-Null
try {
 try{Invoke-BuildStep -Title 'Injected native failure' -Executable $native -Arguments @()}
 catch{if($_.Exception.Message -notlike '*exit 7*'){throw};$expectedFailure=$true;Write-Host $_.Exception.Message}
}finally{Stop-Transcript | Out-Null}
$content=Get-Content -LiteralPath $log -Raw
if(!$expectedFailure -or $content -notlike '*NATIVE_STDOUT_MARKER*' -or $content -notlike '*NATIVE_STDERR_MARKER*'){throw 'Native output or exit code was lost'}
if($ErrorActionPreference -ne 'Stop'){throw 'Error policy was not restored'}
Set-Content -LiteralPath $native -Encoding ASCII -Value "@echo off`r`nexit /b 0"
Invoke-BuildStep -Title 'Injected native success' -Executable $native -Arguments @()
Write-Host 'PASS native stdout/stderr transcript capture, failure exit code, success and error preference restoration'
