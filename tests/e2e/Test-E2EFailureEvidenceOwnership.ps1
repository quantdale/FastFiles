$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$module = Import-Module (Join-Path $repo 'verify/e2e/FastFilesE2E.psm1') -Force -PassThru
$parent = [IO.Path]::GetFullPath((Join-Path $repo 'build/e2e-failure-ownership'))
$root = Join-Path $parent ([guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $root
$marker = Join-Path $root '.owned-e2e-failure-ownership'
[IO.File]::WriteAllText($marker, 'Owned headless evidence regression')
$failures = 0
function Check([bool]$condition, [string]$description) {
    if ($condition) { Write-Host "PASS: $description" }
    else { $script:failures++; Write-Host "FAIL: $description" }
}
try {
    $inner = & $module {
        param($ownedRoot)
        Invoke-E2EScenarioSafely -Id 'inner' -ArtifactRoot $ownedRoot -Body {
            $path = Join-Path $ownedRoot 'inner'
            $null = New-Item -ItemType Directory -Path $path -Force
            [IO.File]::WriteAllText((Join-Path $path 'failure.json'), '{"scenario":"inner","stage":"inner-stage","observation":{"marker":"live inner evidence"}}')
            [IO.File]::WriteAllText((Join-Path $path 'failure.tree.txt'), 'inner UIA tree sentinel')
            [IO.File]::WriteAllText((Join-Path $path 'failure.png'), 'inner screenshot sentinel')
            New-E2EResult -Id 'inner' -Status 'FAIL' -Stage 'inner-stage' -Detail 'inner failure' -DurationMs 17
        }
    } $root
    $innerJson = Get-Content -LiteralPath (Join-Path $root 'inner/failure.json') -Raw | ConvertFrom-Json
    Check ($inner.status -eq 'FAIL' -and $inner.stage -eq 'inner-stage') 'inner failure remains terminal'
    Check ($innerJson.observation.marker -eq 'live inner evidence') 'outer wrapper preserves the current inner failure envelope'
    Check (([IO.File]::ReadAllText((Join-Path $root 'inner/failure.tree.txt'))) -eq 'inner UIA tree sentinel' -and
           ([IO.File]::ReadAllText((Join-Path $root 'inner/failure.png'))) -eq 'inner screenshot sentinel') 'inner tree and image remain intact'
    $innerOuterPath = Join-Path $root 'inner/failure-outer.json'
    Check (Test-Path -LiteralPath $innerOuterPath -PathType Leaf) 'outer boundary writes a separate supplemental envelope'
    if (Test-Path -LiteralPath $innerOuterPath -PathType Leaf) {
        $outer = Get-Content -LiteralPath $innerOuterPath -Raw | ConvertFrom-Json
        Check ($outer.scenario -eq 'inner' -and $outer.stage -eq 'inner-stage' -and
               $outer.note -match 'did not capture an additional') 'supplement distinguishes outer observation from preserved inner evidence'
    }

    $missing = & $module {
        param($ownedRoot)
        Invoke-E2EScenarioSafely -Id 'missing' -ArtifactRoot $ownedRoot -Body {
            New-E2EResult -Id 'missing' -Status 'FAIL' -Stage 'before-window' -Detail 'no context' -Data @{ reason = 'owned synthetic failure' }
        }
    } $root
    $missingJson = Get-Content -LiteralPath (Join-Path $root 'missing/failure.json') -Raw | ConvertFrom-Json
    Check ($missing.status -eq 'FAIL' -and $missingJson.stage -eq 'before-window' -and
           $missingJson.observed -match 'owned synthetic failure') 'missing inner evidence receives a fresh honest outer envelope'

    $context = [pscustomobject]@{
        Driver = [pscustomobject]@{ Provider = 'mock'; Root = $null }
        Main = $null
        Launched = [pscustomobject]@{ Id = $PID }
        SessionRoot = $root
        Environment = [pscustomobject]@{ LOCALAPPDATA = (Join-Path $root 'isolated-profile') }
    }
    $withContext = & $module {
        param($ownedRoot, $mockContext)
        Invoke-E2EScenarioSafely -Id 'with-context' -ArtifactRoot $ownedRoot -Context $mockContext -Body {
            $path = Join-Path $ownedRoot 'with-context'
            [IO.File]::WriteAllText((Join-Path $path 'failure.json'), '{"scenario":"with-context","observation":{"marker":"inner with context"}}')
            [IO.File]::WriteAllText((Join-Path $path 'failure-outer.json'), 'inner-owned outer-name sentinel')
            New-E2EResult -Id 'with-context' -Status 'FAIL' -Stage 'owned-context' -Detail 'inner failure'
        }
    } $root $context
    $contextJson = Get-Content -LiteralPath (Join-Path $root 'with-context/failure.json') -Raw | ConvertFrom-Json
    $alternateOuter = @(Get-ChildItem -LiteralPath (Join-Path $root 'with-context') -Filter 'failure-outer-*.json' -File)
    Check ($withContext.status -eq 'FAIL' -and $contextJson.observation.marker -eq 'inner with context') 'outer context does not overwrite inner evidence'
    Check (([IO.File]::ReadAllText((Join-Path $root 'with-context/failure-outer.json'))) -eq 'inner-owned outer-name sentinel' -and
           $alternateOuter.Count -eq 1) 'pre-existing inner supplemental filename is preserved under a unique outer name'

    $thrown = & $module {
        param($ownedRoot)
        Invoke-E2EScenarioSafely -Id 'thrown' -ArtifactRoot $ownedRoot -Body {
            $path = Join-Path $ownedRoot 'thrown'
            $null = New-Item -ItemType Directory -Path $path -Force
            [IO.File]::WriteAllText((Join-Path $path 'failure.json'), '{"scenario":"thrown","stage":"inner-before-throw","observation":{"marker":"before exception"}}')
            throw 'owned outer exception'
        }
    } $root
    $thrownJson = Get-Content -LiteralPath (Join-Path $root 'thrown/failure.json') -Raw | ConvertFrom-Json
    Check ($thrown.status -eq 'FAIL' -and $thrown.stage -eq 'exception') 'exception remains a terminal failure'
    Check ($thrownJson.observation.marker -eq 'before exception') 'exception path preserves current inner evidence'
    $thrownOuterPath = Join-Path $root 'thrown/failure-outer.json'
    if (Test-Path -LiteralPath $thrownOuterPath -PathType Leaf) {
        $outer = Get-Content -LiteralPath $thrownOuterPath -Raw | ConvertFrom-Json
        Check ($outer.stage -eq 'exception' -and $outer.error -eq 'owned outer exception') 'exception is retained separately'
    } else { Check $false 'exception is retained separately' }

    $stale = Join-Path $root 'stale'
    $null = New-Item -ItemType Directory -Path $stale
    [IO.File]::WriteAllText((Join-Path $stale 'failure.json'), 'stale external evidence')
    $staleResult = & $module {
        param($ownedRoot)
        Invoke-E2EScenarioSafely -Id 'stale' -ArtifactRoot $ownedRoot -Body {
            [IO.File]::WriteAllText((Join-Path $ownedRoot 'stale-body-ran'), 'unexpected')
            New-E2EResult -Id 'stale' -Status 'FAIL' -Stage 'unexpected' -Detail 'unexpected'
        }
    } $root
    Check ($staleResult.status -eq 'FAIL' -and $staleResult.stage -eq 'artifact-directory-not-fresh' -and
           [string]::IsNullOrEmpty($staleResult.artifactDir)) 'pre-existing scenario directory is rejected as current evidence'
    Check (([IO.File]::ReadAllText((Join-Path $stale 'failure.json'))) -eq 'stale external evidence' -and
           -not (Test-Path -LiteralPath (Join-Path $root 'stale-body-ran'))) 'stale evidence is preserved and its scenario body does not run'

    [IO.File]::WriteAllText((Join-Path $root 'occupied-file'), 'external file sentinel')
    $occupied = & $module {
        param($ownedRoot)
        Invoke-E2EScenarioSafely -Id 'occupied-file' -ArtifactRoot $ownedRoot -Body { throw 'must not run' }
    } $root
    Check ($occupied.status -eq 'FAIL' -and $occupied.stage -eq 'artifact-directory-not-fresh' -and
           ([IO.File]::ReadAllText((Join-Path $root 'occupied-file'))) -eq 'external file sentinel') 'existing file at evidence path is preserved and rejected'

    $oldRun = Join-Path $root 'prior-run'
    $null = New-Item -ItemType Directory -Path $oldRun
    [IO.File]::WriteAllText((Join-Path $oldRun 'summary.json'), 'prior summary sentinel')
    $rootRefused = $false
    try {
        Invoke-FastFilesE2E -Exe 'unused-executable' -RepoRoot $repo -ArtifactsDir $oldRun -Filter restore | Out-Null
    } catch { $rootRefused = $_.Exception.Message -match 'Refusing non-empty' }
    Check ($rootRefused -and ([IO.File]::ReadAllText((Join-Path $oldRun 'summary.json'))) -eq 'prior summary sentinel') `
          'direct runner rejects a stale artifact root before any UIA work'

    $emptyRun = Join-Path $root 'empty-run'
    $null = New-Item -ItemType Directory -Path $emptyRun
    $emptyResult = & {
        param($modulePath, $artifactRoot, $repoRoot)
        function Get-UiaDriverAvailability { return [pscustomobject]@{ Available = $false; Reason = 'owned-headless-mock' } }
        function New-UiaScenarioContext { throw 'The headless availability mock must prevent any application launch.' }
        $ast = [Management.Automation.Language.Parser]::ParseFile($modulePath, [ref]$null, [ref]$null)
        $definition = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Invoke-FastFilesE2E' }, $true)
        if (-not $definition) { throw 'Actual direct runner function was not found.' }
        . ([scriptblock]::Create($definition.Extent.Text))
        Invoke-FastFilesE2E -Exe 'unused-executable' -RepoRoot $repoRoot -ArtifactsDir $artifactRoot -Filter restore
    } (Join-Path $repo 'verify/e2e/FastFilesE2E.psm1') $emptyRun $repo
    Check ($emptyResult.status -eq 'SKIPPED' -and $emptyResult.results[0].detail -match 'owned-headless-mock') `
          'actual direct runner accepts an existing empty root and reaches headless availability'

    $filterIndex = 0
    foreach ($filter in @('', ' ', ',;', " `t ; ,`n")) {
        $filterIndex++
        $filterRoot = Join-Path $root ('empty-filter-' + $filterIndex)
        $filterProbe = & {
            param($modulePath, $artifactRoot, $repoRoot, $selector)
            $probe = [pscustomobject]@{ availabilityCalls = 0; launchCalls = 0; refused = $false; status = $null }
            function Get-UiaDriverAvailability {
                $probe.availabilityCalls++
                return [pscustomobject]@{ Available = $true; Reason = 'owned-headless-mock' }
            }
            function New-UiaScenarioContext { $probe.launchCalls++; throw 'No application should be launched by this headless test.' }
            $ast = [Management.Automation.Language.Parser]::ParseFile($modulePath, [ref]$null, [ref]$null)
            $definition = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Invoke-FastFilesE2E' }, $true)
            . ([scriptblock]::Create($definition.Extent.Text))
            try {
                $run = Invoke-FastFilesE2E -Exe 'unused-executable' -RepoRoot $repoRoot -ArtifactsDir $artifactRoot -Filter $selector
                $probe.status = $run.status
            } catch { $probe.refused = $_.Exception.Message -match 'E2E filter must select' }
            return $probe
        } (Join-Path $repo 'verify/e2e/FastFilesE2E.psm1') $filterRoot $repo $filter
        Check ($filterProbe.refused -and $filterProbe.availabilityCalls -eq 0 -and $filterProbe.launchCalls -eq 0 -and
               -not (Test-Path -LiteralPath $filterRoot)) "empty selector $filterIndex is rejected before artifacts or UIA"
    }
    $validFilterIndex = 0
    foreach ($filter in @('all', 'restore', 'navigation,storage', "navigation`tstorage", ' navigation ; storage ')) {
        $validFilterIndex++
        $filterRoot = Join-Path $root ('valid-filter-' + $validFilterIndex)
        $validResult = & {
            param($modulePath, $artifactRoot, $repoRoot, $selector)
            function Get-UiaDriverAvailability { return [pscustomobject]@{ Available = $false; Reason = 'valid-selector-headless-mock' } }
            function New-UiaScenarioContext { throw 'The availability mock must prevent application launch.' }
            $ast = [Management.Automation.Language.Parser]::ParseFile($modulePath, [ref]$null, [ref]$null)
            $definition = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Invoke-FastFilesE2E' }, $true)
            . ([scriptblock]::Create($definition.Extent.Text))
            Invoke-FastFilesE2E -Exe 'unused-executable' -RepoRoot $repoRoot -ArtifactsDir $artifactRoot -Filter $selector
        } (Join-Path $repo 'verify/e2e/FastFilesE2E.psm1') $filterRoot $repo $filter
        Check ($validResult.status -eq 'SKIPPED' -and $validResult.skipped -eq 1 -and
               $validResult.results[0].detail -match 'valid-selector-headless-mock') "valid selector $filter remains SKIPPED when UIA is unavailable"
    }
    $unknownRoot = Join-Path $root 'unknown-filter'
    $unknownProbe = & {
        param($modulePath, $artifactRoot, $repoRoot)
        $probe = [pscustomobject]@{ availabilityCalls = 0; refused = $false }
        function Get-UiaDriverAvailability { $probe.availabilityCalls++; return [pscustomobject]@{ Available = $true } }
        $ast = [Management.Automation.Language.Parser]::ParseFile($modulePath, [ref]$null, [ref]$null)
        $definition = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Invoke-FastFilesE2E' }, $true)
        . ([scriptblock]::Create($definition.Extent.Text))
        try { Invoke-FastFilesE2E -Exe 'unused-executable' -RepoRoot $repoRoot -ArtifactsDir $artifactRoot -Filter 'navigation,unknown' | Out-Null }
        catch { $probe.refused = $_.Exception.Message -match 'Unknown E2E filter' }
        return $probe
    } (Join-Path $repo 'verify/e2e/FastFilesE2E.psm1') $unknownRoot $repo
    Check ($unknownProbe.refused -and $unknownProbe.availabilityCalls -eq 0 -and -not (Test-Path -LiteralPath $unknownRoot)) `
          'unknown selector is rejected before artifacts or UIA'
    foreach ($iterations in @(0, -1)) {
        $iterationRoot = Join-Path $root ('invalid-iterations-' + $iterations)
        $iterationProbe = & {
            param($modulePath, $artifactRoot, $repoRoot, $count)
            $probe = [pscustomobject]@{ availabilityCalls = 0; launchCalls = 0; refused = $false; status = $null }
            function Get-UiaDriverAvailability {
                $probe.availabilityCalls++
                return [pscustomobject]@{ Available = $true; Reason = 'owned-headless-mock' }
            }
            function New-UiaScenarioContext { $probe.launchCalls++; throw 'No application should be launched by this headless test.' }
            function Get-Process { param($Name, $ErrorAction) return @() }
            $ast = [Management.Automation.Language.Parser]::ParseFile($modulePath, [ref]$null, [ref]$null)
            foreach ($name in @('Invoke-E2EScenarioSafely', 'Invoke-E2ELifecycleScenario', 'Invoke-FastFilesE2E')) {
                $definition = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $true)
                . ([scriptblock]::Create($definition.Extent.Text))
            }
            try {
                $run = Invoke-FastFilesE2E -Exe 'unused-executable' -RepoRoot $repoRoot -ArtifactsDir $artifactRoot -Filter lifecycle -LifecycleIterations $count
                $probe.status = $run.status
            } catch { $probe.refused = $_.Exception.Message -match 'LifecycleIterations|less than the minimum allowed range' }
            return $probe
        } (Join-Path $repo 'verify/e2e/FastFilesE2E.psm1') $iterationRoot $repo $iterations
        Check ($iterationProbe.refused -and $iterationProbe.availabilityCalls -eq 0 -and $iterationProbe.launchCalls -eq 0 -and
               -not (Test-Path -LiteralPath $iterationRoot)) "nonpositive lifecycle count $iterations is rejected before artifacts or UIA"
    }
    foreach ($iterations in @(1, 2, 5, 2147483647)) {
        $iterationRoot = Join-Path $root ('valid-iterations-' + $iterations)
        $validIterationResult = & {
            param($modulePath, $artifactRoot, $repoRoot, $count)
            function Get-UiaDriverAvailability { return [pscustomobject]@{ Available = $false; Reason = 'valid-iterations-headless-mock' } }
            function New-UiaScenarioContext { throw 'The availability mock must prevent application launch.' }
            $ast = [Management.Automation.Language.Parser]::ParseFile($modulePath, [ref]$null, [ref]$null)
            $definition = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Invoke-FastFilesE2E' }, $true)
            . ([scriptblock]::Create($definition.Extent.Text))
            Invoke-FastFilesE2E -Exe 'unused-executable' -RepoRoot $repoRoot -ArtifactsDir $artifactRoot -Filter lifecycle -LifecycleIterations $count
        } (Join-Path $repo 'verify/e2e/FastFilesE2E.psm1') $iterationRoot $repo $iterations
        Check ($validIterationResult.status -eq 'SKIPPED' -and $validIterationResult.skipped -eq 1 -and
               $validIterationResult.results[0].detail -match 'valid-iterations-headless-mock') "positive lifecycle count $iterations retains genuine prerequisite SKIPPED"
    }
    $internalRefused = & {
        param($modulePath, $artifactRoot, $repoRoot)
        $ast = [Management.Automation.Language.Parser]::ParseFile($modulePath, [ref]$null, [ref]$null)
        $definition = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Invoke-E2ELifecycleScenario' }, $true)
        . ([scriptblock]::Create($definition.Extent.Text))
        try { Invoke-E2ELifecycleScenario -Exe 'unused-executable' -RepoRoot $repoRoot -ArtifactRoot $artifactRoot -Iterations 0 | Out-Null; return $false }
        catch { return $_.Exception.Message -match 'Iterations|less than the minimum allowed range' }
    } (Join-Path $repo 'verify/e2e/FastFilesE2E.psm1') (Join-Path $root 'internal-invalid') $repo
    Check $internalRefused 'internal lifecycle scenario independently rejects a nonpositive iteration count'

    $noInnerThrow = & $module {
        param($ownedRoot)
        Invoke-E2EScenarioSafely -Id 'plain-throw' -ArtifactRoot $ownedRoot -Body { throw 'plain owned exception' }
    } $root
    $plainJson = Get-Content -LiteralPath (Join-Path $root 'plain-throw/failure.json') -Raw | ConvertFrom-Json
    Check ($noInnerThrow.status -eq 'FAIL' -and $plainJson.error -eq 'plain owned exception') 'exception without inner evidence produces an envelope'

    $contextThrow = & $module {
        param($ownedRoot, $mockContext)
        Invoke-E2EScenarioSafely -Id 'context-throw' -ArtifactRoot $ownedRoot -Context $mockContext -Body { throw 'owned context exception' }
    } $root $context
    $contextThrowJson = Get-Content -LiteralPath (Join-Path $root 'context-throw/failure.json') -Raw | ConvertFrom-Json
    Check ($contextThrow.status -eq 'FAIL' -and $contextThrowJson.error -eq 'owned context exception' -and
           $null -ne $contextThrowJson.observation) 'context exception still captures its normal inner evidence envelope'

    $pass = & $module {
        param($ownedRoot)
        Invoke-E2EScenarioSafely -Id 'ordinary-pass' -ArtifactRoot $ownedRoot -Body {
            New-E2EResult -Id 'ordinary-pass' -Status 'PASS' -Detail 'owned success'
        }
    } $root
    Check ($pass.status -eq 'PASS' -and $pass.detail -eq 'owned success') 'normal success remains unchanged'
    $skipped = & $module {
        param($ownedRoot)
        Invoke-E2EScenarioSafely -Id 'ordinary-skip' -ArtifactRoot $ownedRoot -Body {
            New-E2EResult -Id 'ordinary-skip' -Status 'SKIPPED' -Stage 'unavailable' -Detail 'owned skip'
        }
    } $root
    Check ($skipped.status -eq 'SKIPPED' -and $skipped.stage -eq 'unavailable') 'explicit skip remains unchanged'

    foreach ($invalidCase in @('null', 'missing-status', 'unknown-status', 'wrong-id', 'multiple-values', 'bad-duration', 'negative-duration', 'nan-duration', 'infinite-duration', 'null-detail', 'empty-stage')) {
        $invalid = & $module {
            param($ownedRoot, $kind)
            $id = 'invalid-' + $kind
            Invoke-E2EScenarioSafely -Id $id -ArtifactRoot $ownedRoot -Body {
                switch ($kind) {
                    'null' { return $null }
                    'missing-status' { return [pscustomobject]@{ id = $id; detail = 'missing status' } }
                    'unknown-status' { return [pscustomobject]@{ id = $id; status = 'UNKNOWN'; stage = 'complete'; detail = ''; durationMs = 0; data = $null; artifactDir = '' } }
                    'wrong-id' { return New-E2EResult -Id 'other' -Status 'PASS' }
                    'multiple-values' { 'unexpected extra output'; return New-E2EResult -Id $id -Status 'PASS' }
                    'bad-duration' { return [pscustomobject]@{ id = $id; status = 'PASS'; stage = 'complete'; detail = ''; durationMs = 'not-a-number'; data = $null; artifactDir = '' } }
                    'negative-duration' { return [pscustomobject]@{ id = $id; status = 'PASS'; stage = 'complete'; detail = ''; durationMs = -1; data = $null; artifactDir = '' } }
                    'nan-duration' { return [pscustomobject]@{ id = $id; status = 'PASS'; stage = 'complete'; detail = ''; durationMs = [double]::NaN; data = $null; artifactDir = '' } }
                    'infinite-duration' { return [pscustomobject]@{ id = $id; status = 'PASS'; stage = 'complete'; detail = ''; durationMs = [double]::PositiveInfinity; data = $null; artifactDir = '' } }
                    'null-detail' { return [pscustomobject]@{ id = $id; status = 'PASS'; stage = 'complete'; detail = $null; durationMs = 0; data = $null; artifactDir = '' } }
                    'empty-stage' { return [pscustomobject]@{ id = $id; status = 'PASS'; stage = ''; detail = ''; durationMs = 0; data = $null; artifactDir = '' } }
                }
            }
        } $root $invalidCase
        $failurePath = Join-Path $root ('invalid-' + $invalidCase + '/failure.json')
        $validFailure = $invalid.status -eq 'FAIL' -and $invalid.stage -eq 'invalid-result' -and
                        (Test-Path -LiteralPath $failurePath -PathType Leaf)
        Check $validFailure "malformed $invalidCase result fails with a diagnostic envelope"
    }
    $invalidWithInner = & $module {
        param($ownedRoot)
        Invoke-E2EScenarioSafely -Id 'invalid-with-inner' -ArtifactRoot $ownedRoot -Body {
            [IO.File]::WriteAllText((Join-Path $ownedRoot 'invalid-with-inner/failure.json'), '{"observation":{"marker":"inner before invalid result"}}')
            return $null
        }
    } $root
    $invalidInner = Get-Content -LiteralPath (Join-Path $root 'invalid-with-inner/failure.json') -Raw | ConvertFrom-Json
    $invalidOuter = Get-Content -LiteralPath (Join-Path $root 'invalid-with-inner/failure-outer.json') -Raw | ConvertFrom-Json
    Check ($invalidWithInner.status -eq 'FAIL' -and $invalidWithInner.stage -eq 'invalid-result' -and
           $invalidInner.observation.marker -eq 'inner before invalid result' -and $invalidOuter.stage -eq 'invalid-result') `
          'invalid result preserves inner evidence and records its own outer failure'
} finally {
    $resolvedRoot = [IO.Path]::GetFullPath($root)
    $prefix = $parent.TrimEnd([char[]]'\/') + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedRoot.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolvedRoot) -notmatch '^[0-9a-f]{32}$' -or
        -not (Test-Path -LiteralPath $marker -PathType Leaf)) { throw 'Refusing cleanup outside the uniquely owned marked test root.' }
    Remove-Item -LiteralPath $resolvedRoot -Recurse -Force
}
if ($failures) { throw "$failures E2E failure-evidence ownership checks failed." }
Write-Host 'PASS: E2E failure-evidence ownership checks completed.'
