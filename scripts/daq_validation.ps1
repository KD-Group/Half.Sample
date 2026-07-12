[CmdletBinding(DefaultParameterSetName = 'Help')]
param(
    [ValidateSet('mock', 'legacy', 'xnavi')][string]$Variant,
    [string]$Config,
    [string]$OutputDir,
    [Parameter(ParameterSetName = 'All')][switch]$All,
    [Parameter(ParameterSetName = 'Case')][string]$Case,
    [Parameter(ParameterSetName = 'From')][string]$From,
    [switch]$Quiet
)

function Invoke-DaqValidation {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Executable,
        [Parameter(Mandatory)][string[]]$Arguments,
        [int]$ExpectedExit = 0,
        [string]$ExpectedResult = 'PASS',
        [string]$ExpectedCode = '',
        [string]$ExpectedCase = '',
        [string]$ExpectedCaseCode = '',
        [string]$ExpectedFailedCase = '',
        [string]$ExpectedFailedCode = '',
        [string[]]$ExpectedSupportedStrategies = @(),
        [ValidateSet('Contains','Exact')][string]$StrategyMatch = 'Contains',
        [string[]]$RequiredTopLevelFields = @(),
        [string[]]$RequiredEvidenceProperties = @(),
        [string[]]$RequiredNonEmptyEvidenceFields = @(),
        [switch]$Quiet
    )

    if (-not $Quiet) {
        $displayArguments = @($Arguments | ForEach-Object {
            if ($_ -match '[\s`"]') { "'" + ($_ -replace "'", "''") + "'" } else { $_ }
        })
        Write-Host "[DAQ][RUN] $Executable $($displayArguments -join ' ')"
    }
    $stdout = @(& $Executable @Arguments)
    $processExit = $LASTEXITCODE
    if (-not $Quiet) { Write-Host "[DAQ][EXIT] exit=$processExit" }
    $lastJson = $stdout | Where-Object { $_ -is [string] -and $_.Trim() } | Select-Object -Last 1
    $payload = $null
    try { if ($lastJson) { $payload = $lastJson | ConvertFrom-Json } } catch { $payload = $null }

    if ($null -eq $payload) {
        $parseMessage = '最后一个非空 stdout 行不是 JSON'
        if (-not $Quiet) {
            Write-Host '[DAQ][FAIL] result=FAIL code=JSON_PARSE_FAILED'
            Write-Host "[DAQ][MESSAGE] $parseMessage"
            Write-Host '[DAQ][EVIDENCE] {}'
            Write-Host "[DAQ][ASSERT] $parseMessage"
        }
        throw "exit=$processExit code=JSON_PARSE_FAILED message=$parseMessage evidence={} validation=$parseMessage"
    }
    $errors = @()
    if ($processExit -ne $ExpectedExit) { $errors += "expected exit=$ExpectedExit" }
    if ($payload.result -ne $ExpectedResult) { $errors += "expected result=$ExpectedResult" }
    if ($ExpectedCode -and $payload.code -ne $ExpectedCode) { $errors += "expected code=$ExpectedCode" }

    if ($ExpectedCase) {
        $caseProperty = $payload.cases.PSObject.Properties[$ExpectedCase]
        if ($null -eq $caseProperty) { $errors += "missing cases.$ExpectedCase" }
        elseif ($caseProperty.Value.code -ne $ExpectedCaseCode) { $errors += "expected cases.$ExpectedCase.code=$ExpectedCaseCode" }
    }
    if ($ExpectedFailedCase) {
        $failedProperty = $payload.failed_cases.PSObject.Properties[$ExpectedFailedCase]
        if ($null -eq $failedProperty) { $errors += "missing failed_cases.$ExpectedFailedCase" }
        elseif ($failedProperty.Value.code -ne $ExpectedFailedCode) { $errors += "expected failed_cases.$ExpectedFailedCase.code=$ExpectedFailedCode" }
    }
    foreach ($field in $RequiredTopLevelFields) {
        if ($null -eq $payload.PSObject.Properties[$field]) { $errors += "missing top-level field $field" }
    }
    foreach ($field in $RequiredEvidenceProperties) {
        if ($null -eq $payload.evidence.PSObject.Properties[$field]) { $errors += "missing evidence property $field" }
    }
    foreach ($field in $RequiredNonEmptyEvidenceFields) {
        $property = $payload.evidence.PSObject.Properties[$field]
        if ($null -eq $property) {
            $errors += "missing evidence property $field"
            continue
        }
        $value = $property.Value
        $isEmpty = $null -eq $value
        if ($value -is [string]) { $isEmpty = [string]::IsNullOrWhiteSpace($value) }
        elseif ($value -is [System.Collections.IEnumerable]) { $isEmpty = @($value).Count -eq 0 }
        if ($isEmpty) { $errors += "empty evidence.$field" }
    }
    if ($ExpectedSupportedStrategies.Count) {
        $actual = @($payload.supported_strategies)
        $missing = @($ExpectedSupportedStrategies | Where-Object { $_ -notin $actual })
        if ($missing.Count) { $errors += "missing supported strategies: $($missing -join ',')" }
        if ($StrategyMatch -eq 'Exact') {
            $unexpected = @($actual | Where-Object { $_ -notin $ExpectedSupportedStrategies })
            if ($unexpected.Count) { $errors += "unexpected supported strategies: $($unexpected -join ',')" }
        }
    }
    if ($errors.Count) {
        $evidence = $payload.evidence | ConvertTo-Json -Compress -Depth 8
        if (-not $Quiet) {
            Write-Host "[DAQ][FAIL] result=$($payload.result) code=$($payload.code)"
            Write-Host "[DAQ][MESSAGE] $($payload.message)"
            Write-Host "[DAQ][EVIDENCE] $evidence"
            foreach ($validationError in $errors) { Write-Host "[DAQ][ASSERT] $validationError" }
        }
        throw "exit=$processExit code=$($payload.code) message=$($payload.message) evidence=$evidence validation=$($errors -join '; ')"
    }
    if (-not $Quiet) {
        $evidence = $payload.evidence | ConvertTo-Json -Compress -Depth 8
        Write-Host "[DAQ][PASS] result=$($payload.result) code=$($payload.code)"
        Write-Host "[DAQ][MESSAGE] $($payload.message)"
        Write-Host "[DAQ][EVIDENCE] $evidence"
    }
    return $payload
}

$scriptWasDotSourced = $MyInvocation.InvocationName -eq '.'
if ($scriptWasDotSourced) {
    Write-Host '[DAQ] 已加载 Invoke-DaqValidation'
    return
}

function Show-DaqValidationHelp {
    @'
DAQ 能力验证脚本
用法：
  .\scripts\daq_validation.ps1 -Variant mock|legacy|xnavi -Config <配置文件> [-OutputDir <目录>] (-All | -Case <验证项> | -From <验证项>) [-Quiet]

范围参数必须恰好指定一个：
  -All          按依赖顺序执行全部启用项
  -Case <名称>  执行指定验证项及其依赖
  -From <名称>  从指定验证项重新执行到末尾

示例：
  .\scripts\daq_validation.ps1 -Variant mock -Config src\daq_capability_test\mock_success.tsv -All
  .\scripts\daq_validation.ps1 -Variant xnavi -Config src\daq_capability_test\field_test_matrix.tsv -Case preflight
'@ | Write-Output
}

if ($PSBoundParameters.Count -eq 0) {
    Show-DaqValidationHelp
    exit 0
}

if (-not $Variant) { throw '参数错误：必须指定 -Variant mock|legacy|xnavi' }
if (-not $Config) { throw '参数错误：必须指定 -Config <配置文件>' }
$scopeCount = @($All.IsPresent, -not [string]::IsNullOrWhiteSpace($Case),
    -not [string]::IsNullOrWhiteSpace($From) | Where-Object { $_ }).Count
if ($scopeCount -ne 1) { throw '参数错误：-All、-Case 和 -From 必须恰好指定一个' }

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $repositoryRoot "cpp_build\daq_capability_test_$Variant.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "环境错误：验证程序不存在：$executable；请先执行 scons -Q cpp_build/daq_capability_test_$Variant.exe"
}
if (-not (Test-Path -LiteralPath $Config -PathType Leaf)) {
    throw "参数错误：配置文件不存在：$Config"
}

$suiteArguments = @('suite', '--config', $Config)
if ($All) { $suiteArguments += '--all' }
elseif ($Case) { $suiteArguments += @('--case', $Case) }
else { $suiteArguments += @('--from', $From) }
if ($OutputDir) { $suiteArguments += @('--output-dir', $OutputDir) }

$payload = Invoke-DaqValidation -Executable $executable -Arguments $suiteArguments `
    -ExpectedExit 0 -ExpectedResult PASS -ExpectedCode SUITE_PASSED `
    -RequiredTopLevelFields @('supported_strategies', 'rejected_strategies', 'skipped_cases', 'cases', 'failed_cases') `
    -Quiet:$Quiet
$payload | ConvertTo-Json -Compress -Depth 20
