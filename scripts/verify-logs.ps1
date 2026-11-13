# 敏感日志扫描（v0.12.0）。
#
# 作用：在构建产物、安装包与运行时数据目录里找「密钥形态」的字符串。
# 密钥本来就不该出现在这些地方——所以这里不需要白名单，命中就是问题。
#
# 负例守卫：-SelfTest 会先在临时目录里放一个假密钥，验证扫描器确实能抓到；
# 再放一个干净文件，验证它不会误报。守卫自己坏掉时，门不会一直绿着。

[CmdletBinding()]
param(
    # 要扫描的目录；留空则用默认集合。
    [string[]]$Paths = @(),
    [string]$RepositoryRoot = '',
    # 先跑一次自检：假密钥必须被抓到，干净文件必须不报。
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

# 三类形态：OpenAI 风格密钥、Bearer 头、api_key= 赋值。
$script:Patterns = @(
    @{ Name = 'sk- 密钥';       Regex = 'sk-[A-Za-z0-9_\-]{20,}' },
    @{ Name = 'Bearer 授权头';  Regex = '(?i)authorization\s*:\s*bearer\s+\S{16,}' },
    @{ Name = 'api_key 赋值';   Regex = '(?i)api[_-]?key["'']?\s*[:=]\s*["'']?[A-Za-z0-9_\-]{16,}' }
)

$script:ScannedFiles = 0
$script:Findings = New-Object System.Collections.Generic.List[string]

# 只扫文本类文件：二进制里出现随机字节不算泄露，扫了只会制造噪音。
$textExtensions = @('.jsonl', '.json', '.log', '.txt', '.ini', '.md', '.trace', '.ps1', '.xml', '.yaml', '.yml')
$maxBytes = 4MB

function Test-TextFile {
    param([System.IO.FileInfo]$File)
    if ($textExtensions -notcontains $File.Extension.ToLowerInvariant()) {
        return $false
    }
    if ($File.Length -gt $maxBytes) {
        return $false
    }
    # 前 4KB 里有 NUL 字节就当作二进制跳过。
    $stream = [System.IO.File]::OpenRead($File.FullName)
    try {
        $buffer = New-Object byte[] 4096
        $read = $stream.Read($buffer, 0, $buffer.Length)
        for ($i = 0; $i -lt $read; $i++) {
            if ($buffer[$i] -eq 0) { return $false }
        }
    } finally {
        $stream.Dispose()
    }
    return $true
}

function Invoke-Scan {
    param([string[]]$Targets)

    foreach ($target in $Targets) {
        if (-not (Test-Path -LiteralPath $target)) {
            continue
        }
        $files = Get-ChildItem -LiteralPath $target -Recurse -File -ErrorAction SilentlyContinue
        foreach ($file in $files) {
            if (-not (Test-TextFile -File $file)) {
                continue
            }
            $script:ScannedFiles++
            $lines = Get-Content -LiteralPath $file.FullName -ErrorAction SilentlyContinue
            $lineNumber = 0
            foreach ($line in $lines) {
                $lineNumber++
                foreach ($pattern in $script:Patterns) {
                    if ($line -match $pattern.Regex) {
                        # 只记位置与形态，不把命中的内容打印出来——那等于二次泄露。
                        $script:Findings.Add(("{0}:{1} 命中「{2}」" -f $file.FullName, $lineNumber, $pattern.Name))
                        break
                    }
                }
            }
        }
    }
}

function Invoke-SelfTest {
    Write-Host '==> 负例守卫：扫描器自检' -ForegroundColor Cyan
    $temp = Join-Path ([System.IO.Path]::GetTempPath()) ("aipdf-log-scan-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $temp | Out-Null
    try {
        $dirty = Join-Path $temp 'dirty.log'
        Set-Content -LiteralPath $dirty -Value 'Authorization: Bearer sk-test000000000000000000000000' -Encoding UTF8
        $script:Findings.Clear()
        $script:ScannedFiles = 0
        Invoke-Scan -Targets @($temp)
        if ($script:Findings.Count -eq 0) {
            throw '自检失败：假密钥没有被扫描到，门形同虚设'
        }
        Write-Host "    PASS 假密钥被抓到：$($script:Findings.Count) 处" -ForegroundColor Green

        Set-Content -LiteralPath $dirty -Value '这里只有普通日志：发送 3 块，共 900 字。' -Encoding UTF8
        $script:Findings.Clear()
        $script:ScannedFiles = 0
        Invoke-Scan -Targets @($temp)
        if ($script:Findings.Count -ne 0) {
            throw '自检失败：干净的日志被误报'
        }
        Write-Host '    PASS 干净日志不误报' -ForegroundColor Green
    } finally {
        Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
        $script:Findings.Clear()
        $script:ScannedFiles = 0
    }
}

if ($SelfTest) {
    Invoke-SelfTest
}

$root = if ([string]::IsNullOrEmpty($RepositoryRoot)) {
    (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
} else {
    (Resolve-Path -LiteralPath $RepositoryRoot).Path
}
if ($Paths.Count -eq 0) {
    $Paths = @(
        (Join-Path $root 'build/acceptance'),
        (Join-Path $root 'build/windows-debug/bin'),
        (Join-Path $root 'build/windows-release/bin'),
        (Join-Path $root 'dist'),
        (Join-Path $env:LOCALAPPDATA 'Folixa/ai')
    )
}

Write-Host '==> 扫描敏感字符串' -ForegroundColor Cyan
Write-Host ("    目标: " + ($Paths -join ', ')) -ForegroundColor DarkGray
Invoke-Scan -Targets $Paths

if ($script:Findings.Count -gt 0) {
    foreach ($finding in $script:Findings) {
        Write-Host "FAIL $finding" -ForegroundColor Red
    }
    Write-Host "敏感日志扫描失败：$($script:Findings.Count) 处命中（扫描 $($script:ScannedFiles) 个文件）" -ForegroundColor Red
    exit 1
}

Write-Host "敏感日志扫描通过：扫描 $($script:ScannedFiles) 个文件，未发现密钥形态字符串" -ForegroundColor Green
exit 0
