param(
    [Parameter(Mandatory = $true)]
    [string]$ShaderCross
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path "$PSScriptRoot\.."
$sourceDirectory = Join-Path $root "shaders\dxil"
$outputDirectory = Join-Path $root "assets\shaders\spirv"

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$shaderNames = @(
    "water",
    "lit",
    "unlit",
    "shadow",
    "particle",
    "tex"
)

foreach ($name in $shaderNames) {
    $vertexSource = Join-Path $sourceDirectory "$name.vert.hlsl"
    $fragmentSource = Join-Path $sourceDirectory "$name.frag.hlsl"

    $vertexOutput = Join-Path $outputDirectory "$name.vert.spv"
    $fragmentOutput = Join-Path $outputDirectory "$name.frag.spv"

    Write-Host "Compiling $name vertex shader..."

    & $ShaderCross `
        $vertexSource `
        -s HLSL `
        -d SPIRV `
        -e main `
        --stage vertex `
        -I $sourceDirectory `
        -o $vertexOutput

    if ($LASTEXITCODE -ne 0) {
        throw "Failed to compile $vertexSource"
    }

    Write-Host "Compiling $name fragment shader..."

    & $ShaderCross `
        $fragmentSource `
        -s HLSL `
        -d SPIRV `
        -e main `
        --stage fragment `
        -I $sourceDirectory `
        -o $fragmentOutput

    if ($LASTEXITCODE -ne 0) {
        throw "Failed to compile $fragmentSource"
    }
}

Write-Host "SPIR-V compilation completed."
