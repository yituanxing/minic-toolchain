# Native Windows object-publication regression. Covers a pre-existing COFF
# object, safe overwrite, a rejected assembly, non-file destinations and NUL.
param(
    [Parameter(Mandatory = $true)]
    [string] $MiniAsPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Truth([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw "MINIAS_WINDOWS_ATOMIC_OUTPUT=FAIL $Message" }
}

function Run-MiniAs([string] $InputPath, [string] $OutputPath) {
    & $MiniAsExe '--target=x86_64' '--format=coff' '-o' $OutputPath $InputPath
    return $LASTEXITCODE
}

$MiniAsExe = (Resolve-Path -LiteralPath $MiniAsPath).Path
$Work = Join-Path ([System.IO.Path]::GetTempPath()) ("minias-win-atomic-{0}-{1}" -f $PID, [Guid]::NewGuid().ToString('N'))
New-Item -Path $Work -ItemType Directory | Out-Null
try {
    $InputA = Join-Path $Work 'a.s'
    $InputB = Join-Path $Work 'b.s'
    $InputBad = Join-Path $Work 'bad.s'
    $Output = Join-Path $Work 'existing.obj'
    $Directory = Join-Path $Work 'do-not-replace'
    Set-Content -LiteralPath $InputA -Encoding Ascii -Value ".text`n.globl result`nresult:`n.byte 0x41`n"
    Set-Content -LiteralPath $InputB -Encoding Ascii -Value ".text`n.globl result`nresult:`n.byte 0x42`n"
    Set-Content -LiteralPath $InputBad -Encoding Ascii -Value ".text`n.this_directive_does_not_exist`n"

    Assert-Truth ((Run-MiniAs $InputA $Output) -eq 0) 'initial-output'
    $HashA = (Get-FileHash -LiteralPath $Output -Algorithm SHA256).Hash
    $Security = (Get-Acl -LiteralPath $Output).Sddl
    Assert-Truth ((Run-MiniAs $InputBad $Output) -ne 0) 'invalid-source-accepted'
    Assert-Truth (((Get-FileHash -LiteralPath $Output -Algorithm SHA256).Hash) -eq $HashA) 'invalid-source-destroyed-output'

    Assert-Truth ((Run-MiniAs $InputB $Output) -eq 0) 'regular-replacement'
    $HashB = (Get-FileHash -LiteralPath $Output -Algorithm SHA256).Hash
    Assert-Truth ($HashB -ne $HashA) 'regular-replacement-unchanged'
    Assert-Truth ((Get-Acl -LiteralPath $Output).Sddl -eq $Security) 'regular-replacement-changed-acl'

    New-Item -Path $Directory -ItemType Directory | Out-Null
    Assert-Truth ((Run-MiniAs $InputA $Directory) -ne 0) 'directory-replaced'
    Assert-Truth (Test-Path -LiteralPath $Directory -PathType Container) 'directory-removed'

    Push-Location $Work
    try {
        Assert-Truth ((Run-MiniAs $InputA 'NUL') -eq 0) 'nul-device-not-streamed'
    } finally {
        Pop-Location
    }
    Assert-Truth ((@(
        Get-ChildItem -LiteralPath $Work -Force -Filter '*.minias-tmp-*' -File
    )).Count -eq 0) 'temporary-object-leaked'
    Write-Host 'MINIAS_WINDOWS_ATOMIC_OUTPUT=PASS preserved=source_error,acl regular_replace=1 directory=stream NUL=device temp_cleanup=1'
} finally {
    Remove-Item -LiteralPath $Work -Force -Recurse -ErrorAction SilentlyContinue
}
