[CmdletBinding()]
param(
  [Parameter(Mandatory = $true, Position = 0)]
  [string]$Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$resolved = (Resolve-Path -LiteralPath $Path).Path
$text = [System.IO.File]::ReadAllText($resolved)

# SWIG copies API documentation into adjacent C/C++ string-literal lines. Replace only those
# generated documentation strings: public Python module/type identifiers intentionally remain
# renderdoc and qrenderdoc for compatibility. Fail closed if SWIG ever emits the product name in
# executable code so this build step cannot silently rename a symbol.
foreach($line in ($text -split "`r?`n"))
{
  if($line.Contains('RenderDoc') -and $line -notmatch '^\s*"')
  {
    throw "Unexpected RenderDoc token outside a generated SWIG string literal in '$resolved': $line"
  }
}

$branded = $text.Replace('RenderDoc', 'RenderDic')
if($branded -cne $text)
{
  $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
  [System.IO.File]::WriteAllText($resolved, $branded, $utf8NoBom)
}
