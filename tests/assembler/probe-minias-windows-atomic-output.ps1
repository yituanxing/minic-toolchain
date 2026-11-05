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

# ReplaceFile is documented to preserve the DACL, not the entire security
# descriptor: the new file may legitimately retain a different owner/group.
# Compare every ACE by SID, mask, type and inheritance/protection semantics.
function Get-DaclFingerprint($Security) {
    $Rules = @(
        $Security.GetAccessRules($true, $true,
            [System.Security.Principal.SecurityIdentifier]) |
            ForEach-Object {
                '{0}:{1}:{2}:{3}:{4}:{5}' -f $_.IdentityReference.Value,
                    [int] $_.FileSystemRights,
                    $_.AccessControlType, $_.InheritanceFlags,
                    $_.PropagationFlags, $_.IsInherited
            } | Sort-Object
    )
    return ('protected={0};aces={1}' -f $Security.AreAccessRulesProtected,
        ($Rules -join ';'))
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
    # Install a real, explicit user ACE. Merely comparing two inherited
    # default descriptors would not prove permission merging on replacement.
    $ProbeSecurity = Get-Acl -LiteralPath $Output
    $CurrentUserSid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User
    $ProbeAce = [System.Security.AccessControl.FileSystemAccessRule]::new(
        $CurrentUserSid,
        [System.Security.AccessControl.FileSystemRights]::ReadPermissions,
        [System.Security.AccessControl.InheritanceFlags]::None,
        [System.Security.AccessControl.PropagationFlags]::None,
        [System.Security.AccessControl.AccessControlType]::Allow)
    $ProbeSecurity.AddAccessRule($ProbeAce)
    Set-Acl -LiteralPath $Output -AclObject $ProbeSecurity
    $SecurityBefore = Get-Acl -LiteralPath $Output
    $DaclBefore = Get-DaclFingerprint $SecurityBefore
    $DaclSddlBefore = $SecurityBefore.GetSecurityDescriptorSddlForm(
        [System.Security.AccessControl.AccessControlSections]::Access)
    $HashA = (Get-FileHash -LiteralPath $Output -Algorithm SHA256).Hash
    Assert-Truth ((Run-MiniAs $InputBad $Output) -ne 0) 'invalid-source-accepted'
    Assert-Truth (((Get-FileHash -LiteralPath $Output -Algorithm SHA256).Hash) -eq $HashA) 'invalid-source-destroyed-output'

    Assert-Truth ((Run-MiniAs $InputB $Output) -eq 0) 'regular-replacement'
    $HashB = (Get-FileHash -LiteralPath $Output -Algorithm SHA256).Hash
    Assert-Truth ($HashB -ne $HashA) 'regular-replacement-unchanged'
    $SecurityAfter = Get-Acl -LiteralPath $Output
    $DaclAfter = Get-DaclFingerprint $SecurityAfter
    $DaclSddlAfter = $SecurityAfter.GetSecurityDescriptorSddlForm(
        [System.Security.AccessControl.AccessControlSections]::Access)
    Write-Host (('MINIAS_WINDOWS_ACL_EVIDENCE full_sddl_equal={0} ' +
        'owner_equal={1} group_equal={2} dacl_sddl_equal={3} dacl_aces_equal={4}') -f
        ($SecurityBefore.Sddl -eq $SecurityAfter.Sddl),
        ($SecurityBefore.Owner -eq $SecurityAfter.Owner),
        ($SecurityBefore.Group -eq $SecurityAfter.Group),
        ($DaclSddlBefore -eq $DaclSddlAfter), ($DaclBefore -eq $DaclAfter))
    if($DaclBefore -ne $DaclAfter) {
        Write-Host "MINIAS_WINDOWS_DACL_BEFORE=$DaclBefore"
        Write-Host "MINIAS_WINDOWS_DACL_AFTER=$DaclAfter"
        Write-Host "MINIAS_WINDOWS_DACL_SDDL_BEFORE=$DaclSddlBefore"
        Write-Host "MINIAS_WINDOWS_DACL_SDDL_AFTER=$DaclSddlAfter"
    }
    Assert-Truth ($DaclBefore -eq $DaclAfter) 'regular-replacement-changed-dacl-aces'

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
