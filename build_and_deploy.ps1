# FastFiles build, test, and (opt-in) commit/push script
# Run this from the FastFiles repo root in Visual Studio Developer PowerShell.
# Git mutation is never performed by default. To commit and push, pass
# -Commit together with -CommitMessage (required when -Commit is used).
# Examples:
#   ./build_and_deploy.ps1                                     # build + test only
#   ./build_and_deploy.ps1 -SkipTests                          # build only
#   ./build_and_deploy.ps1 -Commit -CommitMessage "fix: ..."   # build + test + commit + push

param(
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$Commit,
    [string]$CommitMessage = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Write-Host "=== FastFiles Build & Deploy Script ===" -ForegroundColor Cyan

# Verify we're in the right directory
if (-not (Test-Path "CMakeLists.txt")) {
    Write-Error "CMakeLists.txt not found. Please run this script from the FastFiles repo root."
}

# Step 1: Configure
if (-not $SkipBuild) {
    Write-Host "`n[1/4] Configuring with cmake --preset debug..." -ForegroundColor Yellow
    $env:FASTFILES_NINJA_EXE = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    cmake --preset debug
    if ($LASTEXITCODE -ne 0) {
        Write-Error "CMake configuration failed."
    }
    Write-Host "Configuration successful." -ForegroundColor Green
} else {
    Write-Host "`n[1/4] Skipping build (--SkipBuild)" -ForegroundColor DarkGray
}

# Step 2: Build
if (-not $SkipBuild) {
    Write-Host "`n[2/4] Building with cmake --build --preset debug..." -ForegroundColor Yellow
    cmake --build --preset debug
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed."
    }
    Write-Host "Build successful." -ForegroundColor Green
} else {
    Write-Host "`n[2/4] Skipping build (--SkipBuild)" -ForegroundColor DarkGray
}

# Step 3: Test
if (-not $SkipTests) {
    Write-Host "`n[3/4] Running tests with ctest --preset debug..." -ForegroundColor Yellow
    ctest --preset debug --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Some tests failed. Review output above."
    } else {
        Write-Host "All tests passed." -ForegroundColor Green
    }
} else {
    Write-Host "`n[3/4] Skipping tests (--SkipTests)" -ForegroundColor DarkGray
}

# Step 4: Commit and push (opt-in; never runs unless -Commit is passed)
if ($Commit) {
    if (-not $CommitMessage) {
        Write-Error "A commit message is required: pass -CommitMessage when using -Commit."
    }

    Write-Host "`n[4/4] Committing and pushing..." -ForegroundColor Yellow
    
    # Check git status
    $status = git status --short
    if (-not $status) {
        Write-Host "No changes to commit." -ForegroundColor Green
        return
    }
    
    Write-Host "`nChanges to commit:" -ForegroundColor Cyan
    $status | ForEach-Object { Write-Host "  $_" }
    
    git add -A
    git commit -m $CommitMessage
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Git commit failed."
    }
    
    git push
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Git push failed."
    }
    
    Write-Host "Commit and push successful." -ForegroundColor Green
} else {
    Write-Host "`n[4/4] Skipping commit/push (not requested; pass -Commit to enable)" -ForegroundColor DarkGray
}

Write-Host "`n=== Done ===" -ForegroundColor Cyan
