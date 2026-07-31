<#
    Minimal JSON-Schema (draft-07 subset) validator: required, type, enum, pattern,
    properties, items. No external dependency (no network, no NuGet) — the harness
    that validates the build must not itself require a build/install step.
#>

function Test-JsonSchema {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] $Data,
        [Parameter(Mandatory)] [pscustomobject] $Schema
    )

    $errors = [System.Collections.Generic.List[string]]::new()
    Test-JsonSchemaNode -Data $Data -Schema $Schema -Path '$' -Errors $errors
    [pscustomobject]@{
        Valid  = ($errors.Count -eq 0)
        Errors = $errors
    }
}

function Test-JsonSchemaNode {
    param($Data, $Schema, [string]$Path, $Errors)

    if ($null -ne $Schema.type) {
        if (-not (Test-JsonType -Data $Data -Type $Schema.type)) {
            $Errors.Add("$Path : expected type '$($Schema.type)'")
            return
        }
    }

    if ($null -ne $Schema.enum) {
        $allowed = @($Schema.enum)
        if ($allowed -notcontains $Data) {
            $Errors.Add("$Path : value '$Data' not in enum [$($allowed -join ', ')]")
        }
    }

    if ($null -ne $Schema.pattern -and $null -ne $Data) {
        if ($Data -isnot [string] -or -not ($Data -match $Schema.pattern)) {
            $Errors.Add("$Path : value '$Data' does not match pattern '$($Schema.pattern)'")
        }
    }

    if ($Schema.type -eq 'object' -and $null -ne $Data) {
        if ($Schema.required) {
            foreach ($req in $Schema.required) {
                $has = $false
                if ($Data -is [System.Collections.IDictionary]) {
                    $has = $Data.Contains($req)
                } else {
                    $has = $null -ne ($Data.PSObject.Properties.Name | Where-Object { $_ -eq $req })
                }
                if (-not $has) {
                    $Errors.Add("$Path : missing required property '$req'")
                }
            }
        }
        if ($Schema.properties) {
            foreach ($propName in $Schema.properties.PSObject.Properties.Name) {
                $propValue = Get-PropertyValue -Data $Data -Name $propName
                if ($null -ne $propValue) {
                    Test-JsonSchemaNode -Data $propValue -Schema $Schema.properties.$propName -Path "$Path.$propName" -Errors $Errors
                }
            }
        }
    }

    if ($Schema.type -eq 'array' -and $null -ne $Data -and $Schema.items) {
        $i = 0
        foreach ($item in @($Data)) {
            Test-JsonSchemaNode -Data $item -Schema $Schema.items -Path "$Path[$i]" -Errors $Errors
            $i++
        }
    }
}

function Get-PropertyValue {
    param($Data, [string]$Name)
    if ($Data -is [System.Collections.IDictionary]) {
        if ($Data.Contains($Name)) { return $Data[$Name] }
        return $null
    }
    $prop = $Data.PSObject.Properties[$Name]
    if ($prop) { return $prop.Value }
    return $null
}

function Test-JsonType {
    param($Data, [string]$Type)
    switch ($Type) {
        'string'  { return $Data -is [string] }
        'integer' { return ($Data -is [int]) -or ($Data -is [long]) -or ($Data -is [double] -and ($Data -eq [math]::Truncate($Data))) }
        'number'  { return ($Data -is [int]) -or ($Data -is [long]) -or ($Data -is [double]) }
        'boolean' { return $Data -is [bool] }
        'object'  { return ($Data -is [System.Collections.IDictionary]) -or ($Data -is [pscustomobject]) }
        'array'   { return ($Data -is [System.Collections.IEnumerable]) -and ($Data -isnot [string]) -and ($Data -isnot [System.Collections.IDictionary]) }
        default   { return $true }
    }
}

Export-ModuleMember -Function Test-JsonSchema
