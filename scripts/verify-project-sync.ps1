param(
    [string]$SourceProject = (Join-Path $PSScriptRoot '..\source\d3d8-wrapper.vcxproj'),
    [string]$GeneratedProject = (Join-Path $PSScriptRoot '..\build\d3d8-wrapper.vcxproj')
)

$ErrorActionPreference = 'Stop'

function Get-ProjectItems([string]$project, [string]$kind) {
    $project = (Resolve-Path -LiteralPath $project).Path
    [xml]$xml = Get-Content -LiteralPath $project -Raw
    $items = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($node in $xml.SelectNodes("//*[local-name()='$kind' and @Include]")) {
        $path = [System.IO.Path]::GetFullPath((Join-Path (Split-Path $project) $node.Include))
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing $kind file: $path" }
        if (-not $items.Add($path)) { throw "Duplicate $kind entry in ${project}: $path" }
    }
    return ,$items
}

foreach ($kind in @('ClCompile', 'ClInclude')) {
    $source = Get-ProjectItems $SourceProject $kind
    $generated = Get-ProjectItems $GeneratedProject $kind
    $filters = Get-ProjectItems "$SourceProject.filters" $kind
    $missingSource = @($generated | Where-Object { -not $source.Contains($_) })
    $missingGenerated = @($source | Where-Object { -not $generated.Contains($_) })
    if ($missingSource.Count -or $missingGenerated.Count) {
        Write-Host "Project $kind lists differ."
        $missingSource | ForEach-Object { Write-Host "  Missing from source project: $_" }
        $missingGenerated | ForEach-Object { Write-Host "  Missing from generated project: $_" }
        exit 1
    }
    $missingFilters = @($source | Where-Object { -not $filters.Contains($_) })
    $staleFilters = @($filters | Where-Object { -not $source.Contains($_) })
    if ($missingFilters.Count -or $staleFilters.Count) {
        Write-Host "Visual Studio $kind filters differ from the source project."
        $missingFilters | ForEach-Object { Write-Host "  Missing from filters: $_" }
        $staleFilters | ForEach-Object { Write-Host "  Stale filter entry: $_" }
        exit 1
    }
    Write-Host "$kind lists match ($($source.Count) files)."
}
