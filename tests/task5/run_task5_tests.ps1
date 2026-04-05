param(
    [string]$CompilerPath = "S:\CLionProjects\MyCompiler\cmake-build-debug\MyCompiler.exe"
)

$ErrorActionPreference = "Stop"

function New-CleanDirectory {
    param([string]$Path)

    if (Test-Path $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }

    New-Item -ItemType Directory -Path $Path | Out-Null
}

function Invoke-CompilerCase {
    param(
        [string]$Name,
        [string]$InputPath,
        [bool]$ShouldSucceed,
        [string[]]$ExpectedOutputSubstrings = @(),
        [string[]]$ExpectedAsmSubstrings = @(),
        [string[]]$ForbiddenAsmSubstrings = @()
    )

    $tmpRoot = Join-Path $PSScriptRoot "tmp"
    $caseRoot = Join-Path $tmpRoot $Name
    $astDir = Join-Path $caseRoot "ast"
    $cfgDir = Join-Path $caseRoot "cfg"

    New-CleanDirectory -Path $caseRoot
    New-Item -ItemType Directory -Path $astDir | Out-Null
    New-Item -ItemType Directory -Path $cfgDir | Out-Null

    $inputFile = Get-Item -LiteralPath $InputPath
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($inputFile.Name)
    $asmPath = Join-Path $caseRoot ($baseName + ".asm")

    $stdoutPath = Join-Path $caseRoot "stdout.txt"
    $stderrPath = Join-Path $caseRoot "stderr.txt"

    Push-Location $caseRoot
    try {
        $process = Start-Process -FilePath $CompilerPath `
            -ArgumentList @($inputFile.FullName, $astDir, $cfgDir) `
            -WorkingDirectory $caseRoot `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath `
            -Wait `
            -PassThru `
            -NoNewWindow
    } finally {
        Pop-Location
    }

    $output = ""
    if (Test-Path -LiteralPath $stdoutPath) {
        $output += Get-Content -LiteralPath $stdoutPath | Out-String
    }
    if (Test-Path -LiteralPath $stderrPath) {
        $output += Get-Content -LiteralPath $stderrPath | Out-String
    }

    $hasAsm = Test-Path -LiteralPath $asmPath
    if ($ShouldSucceed -and -not $hasAsm) {
        throw "Case '$Name' expected ASM output, but compilation failed. Output:`n$output"
    }

    if (-not $ShouldSucceed -and $hasAsm) {
        throw "Case '$Name' expected failure, but ASM output was produced. Output:`n$output"
    }

    foreach ($expected in $ExpectedOutputSubstrings) {
        if ($output -notlike "*$expected*") {
            throw "Case '$Name' output does not contain expected text '$expected'. Output:`n$output"
        }
    }

    if ($ShouldSucceed) {
        $asmText = Get-Content -LiteralPath $asmPath | Out-String
        foreach ($expectedAsm in $ExpectedAsmSubstrings) {
            if ($asmText -notlike "*$expectedAsm*") {
                throw "Case '$Name' ASM does not contain expected text '$expectedAsm'. ASM:`n$asmText"
            }
        }
        foreach ($forbiddenAsm in $ForbiddenAsmSubstrings) {
            if ($asmText -like "*$forbiddenAsm*") {
                throw "Case '$Name' ASM contains forbidden text '$forbiddenAsm'. ASM:`n$asmText"
            }
        }
    }

    Write-Host "[PASS] $Name"
}

if (-not (Test-Path -LiteralPath $CompilerPath)) {
    throw "Compiler not found: $CompilerPath"
}

$inputRoot = Join-Path $PSScriptRoot "inputs"
$exampleRoot = Join-Path (Join-Path $PSScriptRoot "..\..") "examples\task5"
New-CleanDirectory -Path (Join-Path $PSScriptRoot "tmp")

Invoke-CompilerCase -Name "valid_member_access" `
    -InputPath (Join-Path $exampleRoot "member_access.txt") `
    -ShouldSucceed $true `
    -ExpectedOutputSubstrings @("ASM saved to: member_access.asm") `
    -ExpectedAsmSubstrings @("Point_sum", "main:", "TYPEINFO_type_class_Point_size_8:", "TYPEINFO_field_Point_x_int_offset_0:", "TYPEINFO_field_Point_y_int_offset_4:") `
    -ForbiddenAsmSubstrings @(".type ", ".field ", ".implements ")

Invoke-CompilerCase -Name "valid_inheritance_interface" `
    -InputPath (Join-Path $exampleRoot "inheritance_interface.txt") `
    -ShouldSucceed $true `
    -ExpectedOutputSubstrings @("ASM saved to: inheritance_interface.asm") `
    -ExpectedAsmSubstrings @("TYPEINFO_type_interface_IPrinter_size_0:", "TYPEINFO_type_class_ColoredPoint_size_8:", "TYPEINFO_implements_ColoredPoint_IPrinter:", "ColoredPoint_print") `
    -ForbiddenAsmSubstrings @(".type ", ".field ", ".implements ")

Invoke-CompilerCase -Name "valid_overload_and_import_decl" `
    -InputPath (Join-Path $exampleRoot "overload_and_import.txt") `
    -ShouldSucceed $true `
    -ExpectedOutputSubstrings @("ASM saved to: overload_and_import.asm") `
    -ExpectedAsmSubstrings @("MathOps_add_int", "MathOps_add_int_int", "TYPEINFO_type_class_MathOps_size_4:", "TYPEINFO_field_MathOps_value_int_offset_0:") `
    -ForbiddenAsmSubstrings @(".type ", ".field ", ".implements ")

Invoke-CompilerCase -Name "error_forbidden_override" `
    -InputPath (Join-Path $inputRoot "error_forbidden_override.txt") `
    -ShouldSucceed $false `
    -ExpectedOutputSubstrings @("override")

Invoke-CompilerCase -Name "error_missing_interface_impl" `
    -InputPath (Join-Path $inputRoot "error_missing_interface_impl.txt") `
    -ShouldSucceed $false `
    -ExpectedOutputSubstrings @("interface")

Invoke-CompilerCase -Name "error_import_call_not_supported" `
    -InputPath (Join-Path $inputRoot "error_import_call_not_supported.txt") `
    -ShouldSucceed $false `
    -ExpectedOutputSubstrings @("import")

Write-Host "All Task 5 acceptance checks passed."
