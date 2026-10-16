<#
fetch_assets.ps1 —— 从 clean checkout 还原 third_party/llama 二进制（P3 pin 驱动）

  1. 读 VERSION.pin（tag / 两包 URL+sha256+size / headers tag）
  2. 下载两包 → sha256 校验（已存在且 hash 命中则跳过）
  3. 解包：main 包 *.dll + cudart 包 3 DLL → bin/
  4. 取头文件闭包（fetch_headers.py，同 tag）
  5. 由 llama.def（随仓库提交，7KB 文本）经 lib.exe 生成 lib/llama.lib

前置：Python 3.x（下载+取头）、VS2022（lib.exe；或先跑 vcvars64）。
用法（仓库根）：powershell -ExecutionPolicy Bypass -File third_party\llama\fetch_assets.ps1
#>
$ErrorActionPreference = 'Stop'
$LlamaDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# --- 1. 解析 VERSION.pin ---
$pin = @{}
Get-Content (Join-Path $LlamaDir 'VERSION.pin') | ForEach-Object {
    if ($_ -match '^\s*([A-Za-z0-9_]+)\s*=\s*(\S+)\s*$') { $pin[$Matches[1]] = $Matches[2] }
}
foreach ($k in 'pkg_main_name','pkg_main_sha256','pkg_main_url','pkg_cudart_name','pkg_cudart_sha256','pkg_cudart_url','headers_source_tag') {
    if (-not $pin.ContainsKey($k)) { throw "VERSION.pin 缺键：$k" }
}

# --- 2. 下载 + 校验（python urllib：沙箱/代理环境下比 IWR 更稳） ---
function Get-Asset($name, $url, $sha) {
    $dst = Join-Path $LlamaDir $name
    if (Test-Path $dst) {
        $h = (Get-FileHash $dst -Algorithm SHA256).Hash
        if ($h -eq $sha) { Write-Host "HIT  $name（hash 命中，跳过下载）"; return $dst }
        Write-Host "HASH MISMATCH $name（重下）：$h"
    }
    Write-Host "GET  $url"
    python -c "import urllib.request,sys; urllib.request.urlretrieve(sys.argv[1],sys.argv[2],reporthook=lambda b,s,t:(print(f'\r{b*s//1048576}MB',end='') if b%50==0 else None))" $url $dst
    Write-Host ''
    $h = (Get-FileHash $dst -Algorithm SHA256).Hash
    if ($h -ne $sha) { throw "sha256 校验失败：$name`n实测 $h`n期望 $sha" }
    Write-Host "OK   $name（sha256 通过）"
    return $dst
}
$zipMain = Get-Asset $pin['pkg_main_name'] $pin['pkg_main_url'] $pin['pkg_main_sha256']
$zipCudart = Get-Asset $pin['pkg_cudart_name'] $pin['pkg_cudart_url'] $pin['pkg_cudart_sha256']

# --- 3. 解包 → bin/（仅 DLL；工具 exe 不入构建闭包，需要时自行从 zip 取） ---
$bin = Join-Path $LlamaDir 'bin'
New-Item -ItemType Directory -Force $bin | Out-Null
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $tmp | Out-Null
try {
    Expand-Archive $zipMain -DestinationPath (Join-Path $tmp 'main') -Force
    Expand-Archive $zipCudart -DestinationPath (Join-Path $tmp 'cudart') -Force
    $n = 0
    Get-ChildItem (Join-Path $tmp 'main') -Recurse -Filter '*.dll' | ForEach-Object { Copy-Item $_.FullName $bin -Force; $n++ }
    Get-ChildItem (Join-Path $tmp 'cudart') -Recurse -Filter '*.dll' | ForEach-Object { Copy-Item $_.FullName $bin -Force; $n++ }
    Write-Host "OK   bin/ 共 $n 个 DLL"
} finally { Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue }

# --- 4. 头文件闭包 ---
python (Join-Path $LlamaDir 'fetch_headers.py') $pin['headers_source_tag'] (Join-Path $LlamaDir 'include')

# --- 5. 导入库（官方 win 包不带 .lib；由随仓库的 llama.def 生成） ---
$libDir = Join-Path $LlamaDir 'lib'
New-Item -ItemType Directory -Force $libDir | Out-Null
$libExe = Get-Command lib.exe -ErrorAction SilentlyContinue
if (-not $libExe) {
    $cand = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022\*\VC\Tools\MSVC\*\bin\Hostx64\x64\lib.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cand) { $libExe = $cand.FullName } else { throw '找不到 lib.exe：请先跑 vcvars64.bat 或确认 VS2022 已安装' }
}
& $libExe /def:(Join-Path $LlamaDir 'llama.def') /out:(Join-Path $libDir 'llama.lib') /machine:x64 /nologo
Write-Host 'OK   lib/llama.lib'

# --- 6. 验收 ---
foreach ($f in 'bin\llama.dll','bin\ggml-cuda.dll','bin\cudart64_12.dll','include\llama.h','lib\llama.lib') {
    if (-not (Test-Path (Join-Path $LlamaDir $f))) { throw "缺件：$f" }
}
Write-Host 'DONE：third_party 还原完成，下一步 cmake -S . -B build 按 README-build.md 构建'
