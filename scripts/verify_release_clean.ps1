param(
    [string]$ReleaseDirectory = (Join-Path $PSScriptRoot '..\build\Release')
)

$release = [System.IO.Path]::GetFullPath($ReleaseDirectory)
if (-not (Test-Path -LiteralPath $release -PathType Container)) {
    Write-Error "Release directory does not exist: $release"
    exit 1
}

$errors = [System.Collections.Generic.List[string]]::new()
$forbiddenDirectories = @('.test-artifacts', 'Testing')
$forbiddenExtensions = @('.obj', '.lib', '.tlog', '.vcxproj')

Get-ChildItem -LiteralPath $release -Recurse -Force | ForEach-Object {
    if ($_.PSIsContainer) {
        if ($forbiddenDirectories -contains $_.Name) {
            $errors.Add("Forbidden directory: $($_.FullName)")
        }
        return
    }
    if ($_.Name -match '_test\.exe$') {
        $errors.Add("Test executable: $($_.FullName)")
    }
    if ($_.Name -ieq 'Qt5Test.dll') {
        $errors.Add("Test dependency: $($_.FullName)")
    }
    if ($forbiddenExtensions -contains $_.Extension.ToLowerInvariant()) {
        $errors.Add("Build intermediate: $($_.FullName)")
    }
    $relative = $_.FullName.Substring($release.TrimEnd('\').Length).TrimStart('\').Replace('\', '/')
    $allowed = ($relative -ieq 'DeepLearningStudio.exe') -or
               ($relative -match '^[^/]+\.dll$') -or
               ($relative -match '^(platforms|imageformats)/[^/]+\.dll$') -or
               ($relative -match '^logs/studio-[0-9]{8}-[0-9]{6}-?[0-9]{3}-[0-9]+\.log$')
    if (-not $allowed) {
        $errors.Add("File is not on the Release whitelist: $($_.FullName)")
    }
}

if (-not (Test-Path -LiteralPath (Join-Path $release 'DeepLearningStudio.exe'))) {
    $errors.Add('DeepLearningStudio.exe is missing.')
}

if ($errors.Count -gt 0) {
    $errors | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host "Release directory is clean: $release"
