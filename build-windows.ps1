param(
  [string]$LLVMVersion = "21.1.0",
  [string]$LLVMSource = "",
  [string]$LLVMBuild = "",
  [string]$LLVMInstallPrefix = "",
  [string]$CCompiler = "clang",
  [string]$CXXCompiler = "clang++",
  [int]$Jobs = [Environment]::ProcessorCount,
  [switch]$NoClone
)

$ErrorActionPreference = "Stop"

$RepoRoot = $PSScriptRoot

function Get-FullPathOrDefault {
  param([string]$Value, [string]$DefaultValue)
  if ([string]::IsNullOrWhiteSpace($Value)) {
    return [System.IO.Path]::GetFullPath($DefaultValue)
  }
  return [System.IO.Path]::GetFullPath($Value)
}

$LLVMSource = Get-FullPathOrDefault $LLVMSource (Join-Path $RepoRoot "llvm-project-$LLVMVersion")
$LLVMBuild = Get-FullPathOrDefault $LLVMBuild (Join-Path (Join-Path $RepoRoot "build") "llvm-windows")
$LLVMInstallPrefix = Get-FullPathOrDefault $LLVMInstallPrefix (Join-Path (Join-Path $RepoRoot "out") "llvm-windows")
$PatchFile = Join-Path (Join-Path $RepoRoot "patches") "llvm-21.1-vllvm.patch"

function Require-Command {
  param([string]$Name)
  if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
    throw "missing command: $Name"
  }
}

function Clone-LlvmIfNeeded {
  if (Test-Path (Join-Path $LLVMSource "llvm")) {
    return
  }
  if ($NoClone) {
    throw "LLVM source not found: $LLVMSource"
  }

  Require-Command git
  git -c advice.detachedHead=false clone --depth 1 --branch "llvmorg-$LLVMVersion" `
    https://github.com/llvm/llvm-project.git $LLVMSource
  git -C $LLVMSource switch -c "vllvm-llvmorg-$LLVMVersion" *> $null
}

function Copy-VllvmSources {
  $Dst = Join-Path (Join-Path (Join-Path (Join-Path $LLVMSource "llvm") "lib") "Transforms") "VLLVM"
  $DstInclude = Join-Path $Dst "include"
  $PublicInclude = Join-Path (Join-Path (Join-Path (Join-Path $LLVMSource "llvm") "include") "llvm") "Transforms\VLLVM"

  cmake -E make_directory $DstInclude
  cmake -E make_directory $PublicInclude

  cmake -E copy_if_different (Join-Path (Join-Path $RepoRoot "src") "CMakeLists.txt") (Join-Path $Dst "CMakeLists.txt")
  Get-ChildItem -LiteralPath (Join-Path $RepoRoot "src") -Filter *.cpp | ForEach-Object {
    cmake -E copy_if_different $_.FullName (Join-Path $Dst $_.Name)
  }
  Get-ChildItem -LiteralPath (Join-Path (Join-Path $RepoRoot "src") "include") -Filter *.h | ForEach-Object {
    cmake -E copy_if_different $_.FullName (Join-Path $DstInclude $_.Name)
  }
  cmake -E copy_if_different (Join-Path (Join-Path (Join-Path $RepoRoot "src") "include") "VLLVM.h") `
    (Join-Path $PublicInclude "VLLVM.h")
}

function Apply-VllvmPatch {
  $OldErrorActionPreference = $ErrorActionPreference
  try {
    $ErrorActionPreference = "Continue"
    & git -C $LLVMSource apply --reverse --check $PatchFile *> $null
    $ReverseCheckExitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $OldErrorActionPreference
  }

  if ($ReverseCheckExitCode -eq 0) {
    Write-Host "VLLVM clang patch is already applied"
    return
  }

  $OldErrorActionPreference = $ErrorActionPreference
  try {
    $ErrorActionPreference = "Continue"
    & git -C $LLVMSource apply --check $PatchFile *> $null
    $ApplyCheckExitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $OldErrorActionPreference
  }

  if ($ApplyCheckExitCode -ne 0) {
    & git -C $LLVMSource apply --check $PatchFile
    throw "VLLVM clang patch cannot be applied"
  }

  & git -C $LLVMSource apply $PatchFile
  if ($LASTEXITCODE -ne 0) {
    throw "failed to apply VLLVM clang patch"
  }
}

function Configure-AndBuild {
  $Args = @(
    "-S", (Join-Path $LLVMSource "llvm"),
    "-B", $LLVMBuild,
    "-G", "Ninja",
    "-DCMAKE_C_COMPILER=$CCompiler",
    "-DCMAKE_CXX_COMPILER=$CXXCompiler",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    "-DCMAKE_INSTALL_PREFIX=$LLVMInstallPrefix",
    "-DLLVM_ENABLE_PROJECTS=clang;clang-tools-extra;lld",
    "-DLLVM_TARGETS_TO_BUILD=host",
    "-DLLVM_INCLUDE_TESTS=OFF",
    "-DLLVM_INCLUDE_EXAMPLES=OFF",
    "-DLLVM_INCLUDE_BENCHMARKS=OFF",
    "-DLLVM_ENABLE_ZLIB=OFF",
    "-DLLVM_ENABLE_ZSTD=OFF",
    "-DLLVM_ENABLE_LIBXML2=OFF",
    "-DLLVM_ENABLE_TERMINFO=OFF"
  )

  cmake @Args
  cmake --build $LLVMBuild --target clang clangd --parallel $Jobs
}

Require-Command cmake
Require-Command ninja
Require-Command $CCompiler
Require-Command $CXXCompiler

Clone-LlvmIfNeeded
Copy-VllvmSources
Apply-VllvmPatch
Configure-AndBuild

Write-Host "vllvm clang build finished:"
Write-Host "  $(Join-Path (Join-Path $LLVMBuild "bin") "clang.exe")"
Write-Host "  $(Join-Path (Join-Path $LLVMBuild "bin") "clangd.exe")"
Write-Host "example:"
Write-Host "  $(Join-Path (Join-Path $LLVMBuild "bin") "clang.exe") -enstr input.c -o input.exe"
