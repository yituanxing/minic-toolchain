# Real Windows PE/COFF linker and execution oracle.  The MiniAS candidate
# writes a native x86-64 COFF object; MSVC compiles one independent callee,
# link.exe resolves both unresolved calls, and Windows executes the image.
# This is not an ELF/QEMU proxy and must not silently fall back to GNU as.
param(
    [Parameter(Mandatory = $true)]
    [string] $MiniAsPath
)

$ErrorActionPreference = 'Stop'
# ExitProcess(42) is the success oracle; inspect that native exit status below
# instead of having recent PowerShell native-error preferences reject it early.
$PSNativeCommandUseErrorActionPreference = $false
Set-StrictMode -Version Latest

function Assert-Truth([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw "MINIAS_COFF_WINDOWS=FAIL $Message" }
}

function Run-Native([string] $Program, [string[]] $Arguments) {
    & $Program @Arguments
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        throw "MINIAS_COFF_WINDOWS=FAIL command=$Program exit=$code"
    }
}

$MiniAsExe = (Resolve-Path -LiteralPath $MiniAsPath).Path
$Work = Join-Path (Get-Location).Path 'build/coff-windows-native'
New-Item -Path $Work -ItemType Directory -Force | Out-Null
$InputAsm = Join-Path $Work 'entry.s'
$InputC = Join-Path $Work 'helper.c'
$CandidateObj = Join-Path $Work 'entry.obj'
$HelperObj = Join-Path $Work 'helper.obj'
$Executable = Join-Path $Work 'coff-smoke.exe'

@'
.text
.globl start
start:
    subq $40, %rsp
    call external_helper
    movl %eax, %ecx
    call ExitProcess
.data
.globl helper_address
helper_address:
    .quad external_helper
'@ | Set-Content -LiteralPath $InputAsm -Encoding Ascii

@'
int external_helper(void) {
    return 42;
}
'@ | Set-Content -LiteralPath $InputC -Encoding Ascii

Run-Native -Program $MiniAsExe -Arguments @('--target=x86_64', '--format=coff',
    '-o', $CandidateObj, $InputAsm)
Assert-Truth (Test-Path -LiteralPath $CandidateObj) 'missing-candidate-coff'

$Bytes = [System.IO.File]::ReadAllBytes($CandidateObj)
Assert-Truth ($Bytes.Length -ge 60) 'truncated-coff-header'
$Machine = [BitConverter]::ToUInt16($Bytes, 0)
$Sections = [int] [BitConverter]::ToUInt16($Bytes, 2)
$SymbolsAt = [long] [BitConverter]::ToUInt32($Bytes, 8)
$Symbols = [long] [BitConverter]::ToUInt32($Bytes, 12)
$OptionalHeader = [int] [BitConverter]::ToUInt16($Bytes, 16)
Assert-Truth ($Machine -eq 0x8664) "wrong-machine=0x$($Machine.ToString('X4'))"
Assert-Truth ($Sections -ge 2) "missing-text-or-data-section sections=$Sections"
Assert-Truth ($OptionalHeader -eq 0) 'not-a-relocatable-coff-object'
Assert-Truth ($SymbolsAt -gt 0 -and $Symbols -gt 0) 'missing-symbol-table'
Assert-Truth (($SymbolsAt + $Symbols * 18) -le $Bytes.Length) 'invalid-symbol-extent'

$Rel32 = 0
$Addr64 = 0
for ($Index = 0; $Index -lt $Sections; $Index++) {
    $HeaderAt = 20 + $Index * 40
    Assert-Truth (($HeaderAt + 40) -le $Bytes.Length) 'truncated-section-header'
    $RawSize = [long] [BitConverter]::ToUInt32($Bytes, $HeaderAt + 16)
    $RawAt = [long] [BitConverter]::ToUInt32($Bytes, $HeaderAt + 20)
    Assert-Truth (($RawAt + $RawSize) -le $Bytes.Length) 'invalid-section-data-extent'
    $RelocAt = [long] [BitConverter]::ToUInt32($Bytes, $HeaderAt + 24)
    $RelocCount = [int] [BitConverter]::ToUInt16($Bytes, $HeaderAt + 32)
    if ($RelocCount -eq 0) { continue }
    Assert-Truth (($RelocAt + 10 * $RelocCount) -le $Bytes.Length) 'invalid-reloc-extent'
    for ($Rel = 0; $Rel -lt $RelocCount; $Rel++) {
        $EntryAt = [int] ($RelocAt + $Rel * 10)
        $PatchOffset = [long] [BitConverter]::ToUInt32($Bytes, $EntryAt)
        $SymbolIndex = [long] [BitConverter]::ToUInt32($Bytes, $EntryAt + 4)
        Assert-Truth ($PatchOffset -lt $RawSize) 'relocation-outside-section'
        Assert-Truth ($SymbolIndex -lt $Symbols) 'invalid-relocation-symbol-index'
        $RelocType = [BitConverter]::ToUInt16(
            $Bytes, $EntryAt + 8)
        if ($RelocType -eq 0x0004) { $Rel32++ }
        if ($RelocType -eq 0x0001) { $Addr64++ }
    }
}
Assert-Truth ($Rel32 -ge 2) "missing-unresolved-call-rel32 relocations=$Rel32"
Assert-Truth ($Addr64 -ge 1) "missing-data-addr64-relocation relocations=$Addr64"

$SymbolsDump = (& dumpbin.exe /nologo /symbols $CandidateObj | Out-String)
Assert-Truth ($LASTEXITCODE -eq 0) 'dumpbin-symbols-failed'
Assert-Truth ($SymbolsDump -match '(?im)\bUNDEF\b[^\r\n]*\bexternal_helper\b') 'external-helper-not-undefined'
Assert-Truth ($SymbolsDump -match '(?im)\bUNDEF\b[^\r\n]*\bExitProcess\b') 'exitprocess-not-undefined'
Assert-Truth ($SymbolsDump -match '(?im)\bSECT\d+\b[^\r\n]*\bstart\b') 'missing-defined-entry'

Run-Native -Program 'cl.exe' -Arguments @('/nologo', '/c', '/GS-', '/Zl',
    "/Fo$HelperObj", $InputC)
Run-Native -Program 'link.exe' -Arguments @('/nologo', '/machine:x64', '/subsystem:console',
    '/entry:start', '/nodefaultlib', "/out:$Executable",
    $CandidateObj, $HelperObj, 'kernel32.lib')
Assert-Truth (Test-Path -LiteralPath $Executable) 'missing-linked-pe'

$Image = [System.IO.File]::ReadAllBytes($Executable)
Assert-Truth ($Image.Length -gt 0x100) 'truncated-pe-image'
Assert-Truth ($Image[0] -eq 0x4d -and $Image[1] -eq 0x5a) 'missing-mz-header'
$PeAt = [int] [BitConverter]::ToUInt32($Image, 0x3c)
Assert-Truth (($PeAt + 6) -le $Image.Length) 'bad-pe-offset'
Assert-Truth ($Image[$PeAt] -eq 0x50 -and $Image[$PeAt + 1] -eq 0x45 -and
    $Image[$PeAt + 2] -eq 0 -and $Image[$PeAt + 3] -eq 0) 'missing-pe-signature'
Assert-Truth ([BitConverter]::ToUInt16($Image, $PeAt + 4) -eq 0x8664) 'wrong-pe-machine'

& $Executable
$ExitCode = $LASTEXITCODE
Assert-Truth ($ExitCode -eq 42) "native-exit=$ExitCode expected=42"
Write-Host (("MINIAS_COFF_WINDOWS=PASS machine=AMD64 sections={0} " +
             "unresolved_REL32={1} data_ADDR64={2} " +
             "linker=MSVC-link.exe native_exit=42") -f
            $Sections, $Rel32, $Addr64)
# GitHub's pwsh shell epilogue propagates LASTEXITCODE from the final native
# command. Its deliberate exit=42 has already been asserted above, and must
# not turn this successful test script into a failed workflow step.
$global:LASTEXITCODE = 0
