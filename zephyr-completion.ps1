# Zephyr build.py application-name Tab completion for PowerShell.
# Load in your current shell with: . .\zephyr-completion.ps1
# Works with: python .\build.py -f --app 04_threa<Tab>
# Only suggests directories in app/ containing a CMakeLists.txt.

Register-ArgumentCompleter -Native -CommandName python, python.exe, py, py.exe -ScriptBlock {
    param($wordToComplete, $commandAst, $cursorPosition)

    # This completer is registered for Python, so first make sure the command
    # actually runs our Zephyr build.py helper.
    $tokens = @($commandAst.CommandElements | ForEach-Object { $_.Extent.Text })

    $buildScript = $null
    foreach ($token in $tokens) {
        if ($token -match '(^|[\\/])build\.py$') {
            $buildScript = $token
            break
        }
    }

    if (-not $buildScript) {
        return
    }

    # Complete only the value immediately after --app, not other options.
    if ([string]::IsNullOrEmpty($wordToComplete)) {
        $previousIndex = $tokens.Count - 1
    } else {
        $previousIndex = $tokens.Count - 2
    }

    if ($previousIndex -lt 0 -or $tokens[$previousIndex] -ne '--app') {
        return
    }

    # Resolve app/ relative to the build.py file, not an absolute hardcoded path.
    $projectRoot = Split-Path -Path $buildScript -Parent
    if ([string]::IsNullOrEmpty($projectRoot)) {
        $projectRoot = (Get-Location).Path
    }
    $appDirectory = Join-Path -Path $projectRoot -ChildPath 'app'

    if (-not (Test-Path -LiteralPath $appDirectory -PathType Container)) {
        return
    }

    Get-ChildItem -LiteralPath $appDirectory -Directory |
        Where-Object {
            $_.Name.StartsWith($wordToComplete, [System.StringComparison]::OrdinalIgnoreCase) -and
            (Test-Path -LiteralPath (Join-Path $_.FullName 'CMakeLists.txt') -PathType Leaf)
        } |
        Sort-Object Name |
        ForEach-Object {
            [System.Management.Automation.CompletionResult]::new(
                $_.Name,
                $_.Name,
                'ParameterValue',
                "Zephyr app: $($_.Name)"
            )
        }
}
