<#
.SYNOPSIS
Audits the Windows RenderDic release artifacts for stale RenderDoc branding.

.DESCRIPTION
The audit inspects printable ASCII and both byte alignments of UTF-16LE strings,
PE version resources, PE exports, CodeView/PDB records, and JSON keys/values.
Legacy signatures are normalised into exact compatibility atoms and compared
with an artifact-specific allowlist. The allowlist deliberately contains no
wildcard entries such as RENDERDOC_*.

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File `
  .\util\test\audit_renderdic_artifacts.ps1 -ArtifactRoot .\x64\Development

.EXAMPLE
# Run from a Developer PowerShell, or specify dumpbin explicitly.
.\util\test\audit_renderdic_artifacts.ps1 `
  -ArtifactRoot .\Win32\Release `
  -DumpbinPath 'C:\VS\VC\Tools\MSVC\bin\Hostx64\x64\dumpbin.exe'
#>

[CmdletBinding()]
param(
  [Parameter(Mandatory = $true, Position = 0)]
  [string]$ArtifactRoot,

  [ValidateSet('renderdic.dll', 'qrenderdic.exe', 'renderdiccmd.exe', 'renderdicui.exe',
               'renderdicshim32.dll', 'renderdicshim64.dll', 'renderdoc.pyd',
               'qrenderdoc.pyd', 'renderdic.json')]
  [string[]]$ArtifactName = @(),

  [string]$DumpbinPath,

  [switch]$SkipExports
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$knownArtifacts = @(
  'renderdic.dll',
  'qrenderdic.exe',
  'renderdiccmd.exe',
  'renderdicui.exe',
  'renderdicshim32.dll',
  'renderdicshim64.dll',
  'renderdoc.pyd',
  'qrenderdoc.pyd',
  'renderdic.json'
)

# When no explicit -ArtifactName selection is supplied, the artifact directory represents a
# complete product layout. Reject stale upstream outputs instead of silently auditing only the
# renamed copies. Public SDK headers such as renderdoc_app.h are intentionally not file-name
# failures because they are part of the compatibility surface.
$legacyArtifactNames = @(
  'renderdoc.dll',
  'renderdoc.exp',
  'renderdoc.lib',
  'renderdoc.pdb',
  'qrenderdoc.exe',
  'qrenderdoc.pdb',
  'renderdoccmd.exe',
  'renderdoccmd.pdb',
  'renderdocui.exe',
  'renderdocui.pdb',
  'renderdocshim32.dll',
  'renderdocshim32.pdb',
  'renderdocshim64.dll',
  'renderdocshim64.pdb',
  'renderdoc.json',
  'renderdoc.version'
)

# renderdoc.pyd/qrenderdoc.pyd deliberately retain their public module filenames, and therefore
# also produce matching linker sidecars below pymodules. The same names anywhere else still count
# as stale product outputs.
$pythonSidecarNames = @(
  'renderdoc.exp',
  'renderdoc.lib',
  'renderdoc.pdb',
  'qrenderdoc.exp',
  'qrenderdoc.lib',
  'qrenderdoc.pdb'
)

# These names are public replay/Capture API exports. Keep this as an explicit list so adding a new
# compatibility export requires a deliberate audit update instead of being hidden by a wildcard.
$publicAPISignatures = @(
  'RENDERDOC_AllocArrayMem',
  'RENDERDOC_BecomeRemoteServer',
  'RENDERDOC_BeginProfileRegion',
  'RENDERDOC_KillCallback',
  'RENDERDOC_PreviewWindowCallback',
  'RENDERDOC_ProgressCallback',
  'RENDERDOC_CanGlobalHook',
  'RENDERDOC_CanSelfHostedCapture',
  'RENDERDOC_CheckAndroidPackage',
  'RENDERDOC_CheckRemoteServerConnection',
  'RENDERDOC_CreateBugReport',
  'RENDERDOC_CreateRemoteServerConnection',
  'RENDERDOC_CreateTargetControl',
  'RENDERDOC_EndProfileRegion',
  'RENDERDOC_EndSelfHostCapture',
  'RENDERDOC_EnumerateRemoteTargets',
  'RENDERDOC_ExecuteAndInject',
  'RENDERDOC_FloatToHalf',
  'RENDERDOC_FreeArrayMem',
  'RENDERDOC_GetAPI',
  'RENDERDOC_GetCommitHash',
  'RENDERDOC_GetConfigSetting',
  'RENDERDOC_GetCurrentProcessMemoryUsage',
  'RENDERDOC_GetDefaultCaptureOptions',
  'RENDERDOC_GetDeviceProtocolController',
  'RENDERDOC_GetDriverInformation',
  'RENDERDOC_GetLogFile',
  'RENDERDOC_GetLogFileContents',
  'RENDERDOC_GetSupportedDeviceProtocols',
  'RENDERDOC_GetVersionString',
  'RENDERDOC_HalfToFloat',
  'RENDERDOC_InitCamera',
  'RENDERDOC_InitialiseReplay',
  'RENDERDOC_InjectIntoProcess',
  'RENDERDOC_IsGlobalHookActive',
  'RENDERDOC_IsReleaseBuild',
  'RENDERDOC_LogMessage',
  'RENDERDOC_NeedVulkanLayerRegistration',
  'RENDERDOC_NumVerticesPerPrimitive',
  'RENDERDOC_OpenCaptureFile',
  'RENDERDOC_RegisterMemoryRegion',
  'RENDERDOC_RunFunctionalTests',
  'RENDERDOC_RunUnitTests',
  'RENDERDOC_SaveConfigSettings',
  'RENDERDOC_SetColors',
  'RENDERDOC_SetConfigSetting',
  'RENDERDOC_SetDebugLogFile',
  'RENDERDOC_ShutdownReplay',
  'RENDERDOC_StartGlobalHook',
  'RENDERDOC_StartSelfHostCapture',
  'RENDERDOC_StopGlobalHook',
  'RENDERDOC_UnregisterMemoryRegion',
  'RENDERDOC_UpdateInstalledVersionNumber',
  'RENDERDOC_UpdateVulkanLayerRegistration',
  'RENDERDOC_VertexOffset'
)

# Shader entry points, reflection type names, and Android protocol identifiers intentionally keep
# their established uppercase namespace. As with the API list above, every accepted value is named.
$retainedUppercaseSignatures = @(
  'RENDERDOC_AnnotationType',
  'RENDERDOC_AnnotationValue',
  'RENDERDOC_CAPOPTS',
  'RENDERDOC_Capture',
  'RENDERDOC_CheckerboardPS',
  'RENDERDOC_CopyArrayToMS',
  'RENDERDOC_CopyBLASInstanceCS',
  'RENDERDOC_CopyMSToArray',
  'RENDERDOC_CopyShaderTableCS',
  'RENDERDOC_DebugMathOp',
  'RENDERDOC_DebugSamplePS',
  'RENDERDOC_DebugSampleVS',
  'RENDERDOC_DepthCopyArrayPS',
  'RENDERDOC_DepthCopyArrayToMS',
  'RENDERDOC_DepthCopyMSArrayPS',
  'RENDERDOC_DepthCopyMSPS',
  'RENDERDOC_DepthCopyMSToArray',
  'RENDERDOC_DepthCopyPS',
  'RENDERDOC_DiscardFloatPS',
  'RENDERDOC_DiscardIntPS',
  'RENDERDOC_ExecuteIndirectPatchCS',
  'RENDERDOC_Fixed_Color',
  'RENDERDOC_FixedColPS',
  'RENDERDOC_FloatCopyArrayToMS',
  'RENDERDOC_FloatCopyMSToArray',
  'RENDERDOC_FullscreenVS',
  'RENDERDOC_HistogramCS',
  'RENDERDOC_InputButton',
  'RENDERDOC_MeshGS',
  'RENDERDOC_MeshPickCS',
  'RENDERDOC_MeshPS',
  'RENDERDOC_MeshVS',
  'RENDERDOC_PatchAccStructAddressCS',
  'RENDERDOC_PatchShaderTableCS',
  'RENDERDOC_PixelHistoryCopyPixel',
  'RENDERDOC_PixelHistoryFixedColPS',
  'RENDERDOC_PixelHistoryFixedColPSL',
  'RENDERDOC_PixelHistoryUnused',
  'RENDERDOC_PrepareRayIndirectExecuteCS',
  'RENDERDOC_PrepareTLASCopyIndirectExecuteCS',
  'RENDERDOC_PrimitiveIDPS',
  'RENDERDOC_QOResolvePS',
  'RENDERDOC_QuadOverdrawPS',
  'RENDERDOC_ResourceFormatName',
  'RENDERDOC_ResultMinMaxCS',
  'RENDERDOC_SelectedMip',
  'RENDERDOC_SelectedRangeMax',
  'RENDERDOC_SelectedRangeMin',
  'RENDERDOC_SelectedSample',
  'RENDERDOC_SelectedSliceFace',
  'RENDERDOC_TexDim',
  'RENDERDOC_TexDisplayPS',
  'RENDERDOC_TexDisplayVS',
  'RENDERDOC_TexRemapFloat',
  'RENDERDOC_TexRemapSInt',
  'RENDERDOC_TexRemapUInt',
  'RENDERDOC_Text9VS',
  'RENDERDOC_TextPS',
  'RENDERDOC_TextureType',
  'RENDERDOC_TextVS',
  'RENDERDOC_TileMinMaxCS',
  'RENDERDOC_TriangleSizeGS',
  'RENDERDOC_TriangleSizePS',
  'RENDERDOC_YUVAChannels',
  'RENDERDOC_YUVDownsampleRate'
)

# Official URLs are external protocol identifiers and remain RenderDoc URLs in this fork.
$officialURLs = @(
  'https://renderdoc.org',
  'https://renderdoc.org/',
  'https://renderdoc.org/analytics',
  'https://renderdoc.org/bugreporter',
  'https://renderdoc.org/bugreporter/privacy',
  'https://renderdoc.org/bugreporter/report/%1',
  'https://renderdoc.org/builds',
  'https://renderdoc.org/debug_tool.txt',
  'https://renderdoc.org/docs',
  'https://renderdoc.org/docs/how/how_buffer_format.html',
  'https://renderdoc.org/docs/in_application_api.html',
  'https://renderdoc.org/docs/python_api/index.html',
  'https://renderdoc.org/getupdateurl/%1/%2?htmlnotes=1',
  'https://renderdoc.org/tips/%1',
  'https://renderdoc.org/tips/1',
  'https://github.com/baldurk/renderdoc',
  'https://github.com/baldurk/renderdoc-contrib',
  'https://github.com/baldurk/renderdoc/blob/v1.x/qrenderdoc/Code/Interface/Analytics.h',
  'https://github.com/baldurk/renderdoc/commit/%1',
  'https://github.com/baldurk/renderdoc/issues',
  'https://github.com/baldurk/renderdoc/issues/609',
  'https://github.com/baldurk/renderdoc/releases/tag/v%1',
  'https://github.com/baldurk/renderdoc/wiki/GCN-ISA',
  'https://github.com/baldurk/renderdoc/wiki/PySide2'
)

# These strings are file-format identifiers inside .rdc files. Changing them would break capture
# compatibility, so each section name is explicitly registered rather than allowing renderdoc/*.
$rdcSectionSignatures = @(
  'renderdoc/internal/framecapture',
  'renderdoc/internal/resolvedb',
  'renderdoc/ui/bookmarks',
  'renderdoc/ui/notes',
  'renderdoc/ui/resrenames',
  'renderdoc/internal/exthumb',
  'renderdoc/internal/logfile',
  'renderdoc/ui/edits',
  'renderdoc/internal/d3d12core',
  'renderdoc/internal/d3d12sdklayers',
  'renderdoc/internal/embeddedexternalfiles'
)

# A Windows host still controls the official RenderDoc Android server. These are protocol atoms,
# not Windows product branding. Keep this set exact: adding another Android spelling requires a
# deliberate review here.
$androidProtocolSignatures = @(
  'VK_LAYER_RENDERDOC_Capture',
  'ENABLE_VULKAN_RENDERDOC_CAPTURE',
  'RENDERDOC_HOOK_EGL',
  'libVkLayer_GLES_RenderDoc.so',
  'org.renderdoc.renderdoccmd',
  'package:org.renderdoc.',
  '/files/renderdoc.conf',
  'renderdoccmd remoteserver',
  'localabstract:renderdoc_%i',
  'debug.renderdoc.autograntpermissions',
  '/../share/renderdoc/plugins/android/',
  'renderdoc:*',
  'I/renderdoc(',
  'renderdoc'
)

$rgpMarkerSignatures = @(
  'BeginRenderDocRGPCapture======',
  'EndRenderDocRGPCapture======'
)

# Python keeps its public module names. Qualified generated docstrings such as
# renderdoc.ReplayController are normalised to the exact PYTHON_NAMESPACE atom below; the class
# suffix is not treated as an allowlist pattern. Bare mixed-case "RenderDoc" is never a Python atom.
$pythonModuleSignatures = @(
  'renderdoc',
  '_renderdoc',
  'qrenderdoc',
  '_qrenderdoc',
  'pyrenderdoc',
  '_renderdoc_internal',
  '_renderdoc_completer',
  'renderdoc_output_redirector',
  'renderdoc.pyd',
  'renderdoc.so',
  'renderdoc.pdb',
  'qrenderdoc.pdb',
  'PyInit_renderdoc',
  'PyInit__renderdoc',
  'PyInit_qrenderdoc',
  '--renderdoc',
  '--pyrenderdoc'
)

$extensionProtocolSignatures = @(
  'minimum_renderdoc',
  'Metadata.RenderDocVersion'
)

$captureUploadProtocolSignatures = @(
  'application/x-renderdoc-capture'
)

function Add-AtomPrefix([string]$Prefix, [string[]]$Values)
{
  @($Values | ForEach-Object { "$Prefix$_" })
}

$commonCoreAtoms = @()
$commonCoreAtoms += Add-AtomPrefix 'ABI:' $publicAPISignatures
$commonCoreAtoms += Add-AtomPrefix 'RETAINED:' $retainedUppercaseSignatures
$commonCoreAtoms += Add-AtomPrefix 'URL:' $officialURLs
$commonCoreAtoms += Add-AtomPrefix 'RDC_SECTION:' $rdcSectionSignatures

$androidAtoms = Add-AtomPrefix 'ANDROID:' $androidProtocolSignatures
$rgpAtoms = Add-AtomPrefix 'RGP_MARKER:' $rgpMarkerSignatures
$pythonAtoms = Add-AtomPrefix 'PYTHON_ATOM:' $pythonModuleSignatures
$pythonRuntimeAtoms = @($pythonAtoms | Where-Object {
    $_ -cne 'PYTHON_ATOM:renderdoc.pdb' -and $_ -cne 'PYTHON_ATOM:qrenderdoc.pdb'
  })
$extensionAtoms = Add-AtomPrefix 'EXTENSION_PROTOCOL:' $extensionProtocolSignatures
$captureUploadAtoms = Add-AtomPrefix 'CAPTURE_UPLOAD_PROTOCOL:' $captureUploadProtocolSignatures

$allowedByArtifact = @{
  'renderdic.dll' = $commonCoreAtoms + $androidAtoms + $rgpAtoms + $pythonRuntimeAtoms + @(
    'CPP_IDENTIFIER:RenderDoc',
    'SOURCE_PATH:core'
  )
  'qrenderdic.exe' = $commonCoreAtoms + $pythonRuntimeAtoms + $extensionAtoms +
    $captureUploadAtoms + @(
    'PYTHON_NAMESPACE:renderdoc',
    'PYTHON_NAMESPACE:qrenderdoc',
    'SOURCE_PATH:build',
    'SOURCE_PATH:core',
    'SOURCE_PATH:ui'
  )
  'renderdiccmd.exe' = $commonCoreAtoms + @(
    'SOURCE_PATH:core',
    'SOURCE_PATH:cmd'
  )
  'renderdicui.exe' = @(
    'SOURCE_PATH:ui',
    'URL:https://renderdoc.org/'
  )
  'renderdicshim32.dll' = @('SOURCE_PATH:shim')
  'renderdicshim64.dll' = @('SOURCE_PATH:shim')
  'renderdoc.pyd' = $commonCoreAtoms + $pythonAtoms + $extensionAtoms + @(
    'PYTHON_NAMESPACE:renderdoc',
    'PYTHON_NAMESPACE:qrenderdoc',
    'SOURCE_PATH:build',
    'SOURCE_PATH:core',
    'SOURCE_PATH:ui'
  )
  'qrenderdoc.pyd' = $commonCoreAtoms + $pythonAtoms + $extensionAtoms + @(
    'PYTHON_NAMESPACE:renderdoc',
    'PYTHON_NAMESPACE:qrenderdoc',
    'SOURCE_PATH:build',
    'SOURCE_PATH:core',
    'SOURCE_PATH:ui'
  )
  'renderdic.json' = @()
}

# Positive requirements are source-qualified. Exact is the default; Regex is reserved for an
# explicitly anchored shape where build-generated digits vary. This prevents a generic RenderDic
# occurrence elsewhere in the binary from satisfying a Vulkan, GL, path, or VersionInfo check.
$positiveByArtifact = @{
  'renderdic.dll' = @(
    @{ Label = 'Vulkan GetInstanceProcAddr export'; Source = '^Exports$'; Value = 'VK_LAYER_RENDERDIC_CaptureGetInstanceProcAddr' },
    @{ Label = 'Vulkan GetDeviceProcAddr export'; Source = '^Exports$'; Value = 'VK_LAYER_RENDERDIC_CaptureGetDeviceProcAddr' },
    @{ Label = 'Vulkan negotiation export'; Source = '^Exports$'; Value = 'VK_LAYER_RENDERDIC_CaptureNegotiateLoaderLayerInterfaceVersion' },
    @{ Label = 'Vulkan instance-extension export'; Source = '^Exports$'; Value = 'VK_LAYER_RENDERDIC_CaptureEnumerateInstanceExtensionProperties' },
    @{ Label = 'OpenGL tool identity'; Source = '^(ASCII|UTF-16LE-(even|odd))$'; Value = 'RenderDic' },
    @{ Label = 'OpenGL replay window class'; Source = '^(ASCII|UTF-16LE-(even|odd))$'; Value = 'renderdicGLclass' },
    @{ Label = 'OpenGL replay window title'; Source = '^(ASCII|UTF-16LE-(even|odd))$'; Value = 'RenderDic replay window' },
    @{ Label = 'Windows private config'; Source = '^(ASCII|UTF-16LE-(even|odd))$'; Value = 'renderdic.conf' },
    @{ Label = 'Windows capture directory'; Source = '^(ASCII|UTF-16LE-(even|odd))$'; Value = 'RenderDic\%ls_%04d.%02d.%02d_%02d.%02d.rdc' },
    @{ Label = 'Windows symbol cache'; Source = '^(ASCII|UTF-16LE-(even|odd))$'; Value = '\renderdic\symbols;SRV*' },
    @{ Label = 'DLL original filename'; Source = '^VersionInfo:OriginalFilename$'; Value = 'renderdic.dll' },
    @{ Label = 'DLL product name'; Source = '^VersionInfo:ProductName$'; Value = 'RenderDic' }
  )
  'qrenderdic.exe' = @(
    @{ Label = 'UI original filename'; Source = '^VersionInfo:OriginalFilename$'; Value = 'qrenderdic.exe' },
    @{ Label = 'UI product name'; Source = '^VersionInfo:ProductName$'; Value = 'RenderDic' },
    @{ Label = 'UI application identity'; Source = '^(ASCII|UTF-16LE-(even|odd))$'; Value = 'QRenderDic initialising.' },
    @{ Label = 'capture upload MIME protocol'; Source = '^(ASCII|UTF-16LE-(even|odd))$'; Value = 'application/x-renderdoc-capture' }
  )
  'renderdiccmd.exe' = @(
    @{ Label = 'CLI original filename'; Source = '^VersionInfo:OriginalFilename$'; Value = 'renderdiccmd.exe' },
    @{ Label = 'CLI product name'; Source = '^VersionInfo:ProductName$'; Value = 'RenderDic' },
    @{ Label = 'CLI help identity'; Source = '^(ASCII|UTF-16LE-(even|odd))$'; Value = 'Command line tool for capture & replay with RenderDic.' }
  )
  'renderdicui.exe' = @(
    @{ Label = 'UI forward target'; Source = '^(ASCII|UTF-16LE-(even|odd))$'; Value = 'qrenderdic.exe' }
  )
  'renderdicshim32.dll' = @(
    @{ Label = '32-bit global-hook mapping'; Source = '^(ASCII|UTF-16LE-(even|odd))$'; Value = 'RenderDicGlobalHookData32' }
  )
  'renderdicshim64.dll' = @(
    @{ Label = '64-bit global-hook mapping'; Source = '^(ASCII|UTF-16LE-(even|odd))$'; Value = 'RenderDicGlobalHookData64' }
  )
  'renderdoc.pyd' = @(
    @{ Label = 'renderdoc Python module export'; Source = '^Exports$'; Value = 'PyInit_renderdoc' },
    @{ Label = 'branded renderdoc Python docstring'; Source = '^ASCII$'; Match = 'Contains'; Value = 'Initialises RenderDic for replay.' }
  )
  'qrenderdoc.pyd' = @(
    @{ Label = 'qrenderdoc Python module export'; Source = '^Exports$'; Value = 'PyInit_qrenderdoc' },
    @{ Label = 'branded qrenderdoc Python docstring'; Source = '^ASCII$'; Match = 'Contains'; Value = 'internal RenderDic replay controller.' }
  )
  'renderdic.json' = @(
    @{ Label = 'Vulkan layer name'; Source = '^JSON value:'; Value = 'VK_LAYER_RENDERDIC_Capture' },
    @{ Label = 'Vulkan library path'; Source = '^JSON value:'; Value = '.\renderdic.dll' },
    @{ Label = 'Vulkan GetInstanceProcAddr function'; Source = '^JSON value:'; Value = 'VK_LAYER_RENDERDIC_CaptureGetInstanceProcAddr' },
    @{ Label = 'Vulkan GetDeviceProcAddr function'; Source = '^JSON value:'; Value = 'VK_LAYER_RENDERDIC_CaptureGetDeviceProcAddr' },
    @{ Label = 'Vulkan negotiation function'; Source = '^JSON value:'; Value = 'VK_LAYER_RENDERDIC_CaptureNegotiateLoaderLayerInterfaceVersion' }
  )
}

function Find-Dumpbin
{
  if($DumpbinPath)
  {
    if(-not (Test-Path -LiteralPath $DumpbinPath -PathType Leaf))
    {
      throw "dumpbin.exe was not found at '$DumpbinPath'."
    }
    return (Resolve-Path -LiteralPath $DumpbinPath).Path
  }

  $command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
  if($command)
  {
    return $command.Source
  }

  if($env:VCToolsInstallDir)
  {
    $candidate = Join-Path $env:VCToolsInstallDir 'bin\Hostx64\x64\dumpbin.exe'
    if(Test-Path -LiteralPath $candidate -PathType Leaf)
    {
      return $candidate
    }
  }

  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if(Test-Path -LiteralPath $vswhere -PathType Leaf)
  {
    $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
      -property installationPath
    if($install)
    {
      $toolsRoot = Join-Path $install 'VC\Tools\MSVC'
      $candidate = Get-ChildItem -LiteralPath $toolsRoot -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x64\dumpbin.exe' } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
      if($candidate)
      {
        return $candidate
      }
    }
  }

  throw 'dumpbin.exe was not found. Run from a Developer PowerShell, pass -DumpbinPath, or use -SkipExports for an explicitly incomplete local scan.'
}

function Get-RelevantEmbeddedStrings([string]$Path)
{
  $bytes = [System.IO.File]::ReadAllBytes($Path)
  $encodings = @(@{
      Name = 'ASCII'
      Text = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes)
    })

  # A wide string can start at either byte alignment inside a PE section. Decoding only the whole
  # file as UTF-16LE misses the odd-aligned case, so inspect both slices explicitly.
  foreach($offset in @(0, 1))
  {
    $count = $bytes.Length - $offset
    if($count -gt 1)
    {
      if(($count % 2) -ne 0)
      {
        $count--
      }
      $encodings += @{
        Name = if($offset -eq 0) { 'UTF-16LE-even' } else { 'UTF-16LE-odd' }
        Text = [System.Text.Encoding]::Unicode.GetString($bytes, $offset, $count)
      }
    }
  }

  foreach($encoding in $encodings)
  {
    foreach($match in [regex]::Matches($encoding.Text, '[\x20-\x7e]{4,}'))
    {
      $value = $match.Value
      $hasWorkspacePath = $false
      foreach($workspacePath in $workspacePathVariants)
      {
        if($value.IndexOf($workspacePath, [System.StringComparison]::OrdinalIgnoreCase) -ge 0)
        {
          $hasWorkspacePath = $true
          break
        }
      }
      if($value -imatch 'q?renderd[oi]c' -or $value -match 'GlobalHookData' -or
         $value -imatch 'R:\\RenderDicSrc\\' -or $hasWorkspacePath)
      {
        [pscustomobject]@{ Source = $encoding.Name; Value = $value }
      }
    }
  }
}

function Get-VersionInfoStrings([string]$Path)
{
  $version = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
  foreach($property in @('Comments', 'CompanyName', 'FileDescription', 'FileVersion', 'InternalName',
                         'Language', 'LegalCopyright', 'LegalTrademarks', 'OriginalFilename',
                         'PrivateBuild', 'ProductName', 'ProductVersion', 'SpecialBuild'))
  {
    $value = $version.$property
    if($value)
    {
      [pscustomobject]@{ Source = "VersionInfo:$property"; Value = [string]$value }
    }
  }
}

function Get-ExportStrings([string]$Path, [string]$Dumpbin)
{
  $output = & $Dumpbin /nologo /exports $Path 2>&1
  if($LASTEXITCODE -ne 0)
  {
    throw "dumpbin failed for '$Path':`n$($output -join [Environment]::NewLine)"
  }

  $inTable = $false
  foreach($line in $output)
  {
    if($line -match '^\s*ordinal\s+hint\s+RVA\s+name\s*$')
    {
      $inTable = $true
      continue
    }
    if($inTable -and $line -match '^\s*Summary\s*$')
    {
      break
    }
    if($inTable -and $line -match '^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)')
    {
      [pscustomobject]@{ Source = 'Exports'; Value = $Matches[1] }
    }
  }
}

function Get-CodeViewStrings([string]$Path, [string]$Dumpbin)
{
  $output = & $Dumpbin /nologo /headers $Path 2>&1
  if($LASTEXITCODE -ne 0)
  {
    throw "dumpbin /headers failed for '$Path':`n$($output -join [Environment]::NewLine)"
  }

  foreach($line in $output)
  {
    # dumpbin renders an IMAGE_DEBUG_TYPE_CODEVIEW entry as:
    #   Format: RSDS, {guid}, age, path-to.pdb
    # Split only the first three commas so a legal comma in the PDB path remains part of the path.
    if($line -match '^\s*Format:\s*(RSDS|NB10)\s*,')
    {
      $format = $Matches[1]
      $parts = @($line -split '\s*,\s*', 4)
      if($parts.Count -eq 4)
      {
        $pdb = $parts[3].Trim()
        if($pdb -match '(?i)\.pdb$')
        {
          [pscustomobject]@{ Source = "CodeView:$format"; Value = $pdb }
        }
      }
    }
  }
}

function Get-JsonStrings([string]$Path)
{
  $json = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
  function Visit-JsonValue($Value, [string]$JsonPath)
  {
    if($null -eq $Value)
    {
      return
    }
    if($Value -is [System.Management.Automation.PSCustomObject])
    {
      foreach($property in $Value.PSObject.Properties)
      {
        [pscustomobject]@{ Source = "JSON key:$JsonPath"; Value = $property.Name }
        Visit-JsonValue $property.Value "$JsonPath.$($property.Name)"
      }
      return
    }
    if($Value -is [System.Collections.IEnumerable] -and -not ($Value -is [string]))
    {
      $index = 0
      foreach($item in $Value)
      {
        Visit-JsonValue $item "$JsonPath[$index]"
        $index++
      }
      return
    }
    [pscustomobject]@{ Source = "JSON value:$JsonPath"; Value = [string]$Value }
  }
  Visit-JsonValue $json '$'
}

function Get-AuditAtoms([string]$Text, [string]$Artifact)
{
  # Work on a masked copy. Every accepted compatibility family is recognised by an exact spelling
  # or a tightly constrained context, then removed before the final catch-all RenderDoc scan.
  $working = $Text

  $sourcePathPattern = '(?i)R:\\RenderDicSrc\\(?<area>core|ui|cmd|shim|x64|win32)\\[^:*?"<>|\r\n]*?\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx|inl|i|m|mm|rc|ui|natvis|json|xml)'
  foreach($match in [regex]::Matches($working, $sourcePathPattern))
  {
    $area = $match.Groups['area'].Value.ToLowerInvariant()
    if($area -eq 'x64' -or $area -eq 'win32')
    {
      $area = 'build'
    }
    "SOURCE_PATH:$area"
  }
  $working = [regex]::Replace($working, $sourcePathPattern, ' ')

  $urlPattern = '(?i)https?://[^\s<>"'']+'
  foreach($match in [regex]::Matches($working, $urlPattern))
  {
    $url = $match.Value.TrimEnd('.', ',', ';', ':', ')', ']', '}')
    if($url -imatch 'renderdoc')
    {
      "URL:$url"
    }
  }
  $working = [regex]::Replace($working, $urlPattern, ' ')

  foreach($signature in ($rdcSectionSignatures | Sort-Object Length -Descending))
  {
    if($working.Contains($signature))
    {
      "RDC_SECTION:$signature"
      $working = $working.Replace($signature, ' ')
    }
  }

  foreach($signature in ($rgpMarkerSignatures | Sort-Object Length -Descending))
  {
    if($working.Contains($signature))
    {
      "RGP_MARKER:$signature"
      $working = $working.Replace($signature, ' ')
    }
  }

  # The bare Android log tag is accepted only when it is the complete printable atom. It is not a
  # replacement rule, so a sentence containing a stale lowercase product name still fails.
  if($working -ceq 'renderdoc' -and $Artifact -ceq 'renderdic.dll')
  {
    'ANDROID:renderdoc'
    $working = ' '
  }
  foreach($signature in ($androidProtocolSignatures | Where-Object { $_ -cne 'renderdoc' } |
                          Sort-Object Length -Descending))
  {
    if($working.Contains($signature))
    {
      "ANDROID:$signature"
      $working = $working.Replace($signature, ' ')
    }
  }

  foreach($signature in ($extensionProtocolSignatures | Sort-Object Length -Descending))
  {
    $pattern = '(?<![A-Za-z0-9_])' + [regex]::Escape($signature) + '(?![A-Za-z0-9_])'
    if([regex]::IsMatch($working, $pattern))
    {
      "EXTENSION_PROTOCOL:$signature"
      $working = [regex]::Replace($working, $pattern, ' ')
    }
  }

  foreach($signature in ($captureUploadProtocolSignatures | Sort-Object Length -Descending))
  {
    $pattern = '(?<![A-Za-z0-9_])' + [regex]::Escape($signature) + '(?![A-Za-z0-9_])'
    if([regex]::IsMatch($working, $pattern))
    {
      "CAPTURE_UPLOAD_PROTOCOL:$signature"
      $working = [regex]::Replace($working, $pattern, ' ')
    }
  }

  foreach($module in @('renderdoc', 'qrenderdoc'))
  {
    $pattern = '(?<![A-Za-z0-9_])' + [regex]::Escape($module) +
               '(?=\.(?!(?:pyd|pdb|so)(?![A-Za-z0-9_]))[A-Za-z_][A-Za-z0-9_]*)'
    if([regex]::IsMatch($working, $pattern))
    {
      "PYTHON_NAMESPACE:$module"
      $working = [regex]::Replace($working, $pattern, ' ')
    }
  }

  foreach($signature in ($pythonModuleSignatures | Sort-Object Length -Descending))
  {
    $pattern = '(?<![A-Za-z0-9_])' + [regex]::Escape($signature) + '(?![A-Za-z0-9_])'
    if([regex]::IsMatch($working, $pattern))
    {
      "PYTHON_ATOM:$signature"
      $working = [regex]::Replace($working, $pattern, ' ')
    }
  }

  # Keep the long-standing C++ class/namespace as a source-level compatibility identifier. Only
  # C++ qualification and MSVC RTTI encodings are recognised; prose containing bare RenderDoc is
  # deliberately left for the failure scan.
  $cppPatterns = @('RenderDoc::', '\.\?A[UV]RenderDoc@@')
  foreach($pattern in $cppPatterns)
  {
    if([regex]::IsMatch($working, $pattern))
    {
      'CPP_IDENTIFIER:RenderDoc'
      $working = [regex]::Replace($working, $pattern, ' ')
    }
  }

  # Link-time wide-string suffix folding can place an unrelated printable WCHAR immediately before
  # an otherwise exact retained name (for example SRENDERDOC_CopyShaderTableCS). Recognise only
  # signatures from the explicit lists, with a strict right boundary, before scanning unknown
  # uppercase names. This keeps the allowlist exact without treating linker layout as branding.
  foreach($signature in ($publicAPISignatures | Sort-Object Length -Descending))
  {
    $pattern = [regex]::Escape($signature) + '(?![A-Za-z0-9_])'
    if([regex]::IsMatch($working, $pattern))
    {
      "ABI:$signature"
      $working = [regex]::Replace($working, $pattern, ' ')
    }
  }
  foreach($signature in ($retainedUppercaseSignatures | Sort-Object Length -Descending))
  {
    $pattern = [regex]::Escape($signature) + '(?![A-Za-z0-9_])'
    if([regex]::IsMatch($working, $pattern))
    {
      "RETAINED:$signature"
      $working = [regex]::Replace($working, $pattern, ' ')
    }
  }

  $uppercasePattern = '(?<![A-Za-z0-9_])_?(RENDERDOC_[A-Za-z0-9_]+)(?:@\d+)?'
  foreach($match in [regex]::Matches($working, $uppercasePattern))
  {
    $signature = $match.Groups[1].Value
    if($publicAPISignatures -ccontains $signature)
    {
      "ABI:$signature"
    }
    elseif($retainedUppercaseSignatures -ccontains $signature)
    {
      "RETAINED:$signature"
    }
    else
    {
      "LEGACY:$signature"
    }
  }
  $working = [regex]::Replace($working, $uppercasePattern, ' ')

  # Anything left containing the old mixed/lowercase brand is an unregistered exact finding. The
  # surrounding source/protocol token is included to make the failure actionable.
  $legacyPattern = '(?i)[A-Za-z0-9_.\\/@:\-]*q?renderdoc[A-Za-z0-9_.\\/@:\-]*'
  foreach($match in [regex]::Matches($working, $legacyPattern))
  {
    "LEGACY:$($match.Value)"
  }
}

function Get-RenderDicCasingFindings([string]$Text)
{
  $valid = @('RenderDic', 'renderdic', 'RENDERDIC', 'QRenderDic', 'qrenderdic')
  foreach($match in [regex]::Matches($Text, '(?i)q?renderdic'))
  {
    if(-not ($valid -ccontains $match.Value))
    {
      $match.Value
    }
  }
}

function Test-PositiveRequirement($Records, $Requirement)
{
  $matchKind = 'Exact'
  if($Requirement.ContainsKey('Match'))
  {
    $matchKind = $Requirement.Match
  }
  foreach($record in $Records)
  {
    if($record.Source -notmatch $Requirement.Source)
    {
      continue
    }
    if(($matchKind -ceq 'Exact' -and $record.Value -ceq $Requirement.Value) -or
       ($matchKind -ceq 'Contains' -and $record.Value.Contains($Requirement.Value)) -or
       ($matchKind -ceq 'Regex' -and $record.Value -cmatch $Requirement.Value))
    {
      return $true
    }
  }
  return $false
}

$root = (Resolve-Path -LiteralPath $ArtifactRoot).Path
$allFiles = @(Get-ChildItem -LiteralPath $root -File -Recurse)
$workspaceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path.TrimEnd('\')
$workspacePathVariants = @($workspaceRoot, $workspaceRoot.Replace('\', '/'))

if($ArtifactName.Count -gt 0)
{
  $artifacts = @()
  foreach($name in $ArtifactName)
  {
    $matches = @($allFiles | Where-Object { $_.Name -ceq $name })
    if($matches.Count -ne 1)
    {
      throw "Expected exactly one '$name' below '$root', found $($matches.Count)."
    }
    $artifacts += $matches[0]
  }
}
else
{
  $staleFiles = @($allFiles | Where-Object {
      $legacyArtifactNames -icontains $_.Name -and
      -not ($pythonSidecarNames -icontains $_.Name -and $_.Directory.Name -ieq 'pymodules')
    })
  if($staleFiles.Count -gt 0)
  {
    throw ("Stale upstream artifact filename(s) coexist below '$root':`n - {0}" -f
      (($staleFiles.FullName | Sort-Object -Unique) -join "`n - "))
  }

  $artifacts = @($allFiles | Where-Object { $knownArtifacts -ccontains $_.Name })
  foreach($required in @('renderdic.dll', 'qrenderdic.exe', 'renderdiccmd.exe', 'renderdoc.pyd',
                          'qrenderdoc.pyd', 'renderdic.json'))
  {
    if(-not ($artifacts.Name -ccontains $required))
    {
      throw "Required artifact '$required' was not found below '$root'."
    }
  }
  if(-not ($artifacts.Name -ccontains 'renderdicshim32.dll') -and
     -not ($artifacts.Name -ccontains 'renderdicshim64.dll'))
  {
    throw "No RenderDic shim DLL was found below '$root'."
  }
}

if($artifacts.Count -eq 0)
{
  throw "No first-party RenderDic PE/JSON artifacts were found below '$root'."
}

$dumpbin = $null
if(-not $SkipExports -and ($artifacts.Extension -contains '.dll' -or
                           $artifacts.Extension -contains '.exe' -or
                           $artifacts.Extension -contains '.pyd'))
{
  $dumpbin = Find-Dumpbin
}

$failures = @()
foreach($artifact in ($artifacts | Sort-Object FullName))
{
  Write-Host "Auditing $($artifact.FullName)"
  if($artifact.Extension -ieq '.json')
  {
    $records = @(Get-JsonStrings $artifact.FullName)
  }
  else
  {
    $records = @(Get-RelevantEmbeddedStrings $artifact.FullName)
    $records += @(Get-VersionInfoStrings $artifact.FullName)
    if(-not $SkipExports)
    {
      $records += @(Get-ExportStrings $artifact.FullName $dumpbin)
      $records += @(Get-CodeViewStrings $artifact.FullName $dumpbin)
    }
  }

  $allowed = @($allowedByArtifact[$artifact.Name])
  foreach($record in $records)
  {
    foreach($workspacePath in $workspacePathVariants)
    {
      if($record.Value.IndexOf($workspacePath, [System.StringComparison]::OrdinalIgnoreCase) -ge 0)
      {
        $failures += "$($artifact.Name) [$($record.Source)]: real workspace path '$workspacePath' in '$($record.Value)'"
      }
    }

    foreach($casing in @(Get-RenderDicCasingFindings $record.Value))
    {
      $failures += "$($artifact.Name) [$($record.Source)]: invalid RenderDic casing '$casing' in '$($record.Value)'"
    }

    foreach($atom in @(Get-AuditAtoms $record.Value $artifact.Name))
    {
      if(-not ($allowed -ccontains $atom))
      {
        $failures += "$($artifact.Name) [$($record.Source)]: '$atom' in '$($record.Value)'"
      }
    }
  }

  foreach($requirement in $positiveByArtifact[$artifact.Name])
  {
    if($SkipExports -and $requirement.Source -ceq '^Exports$')
    {
      continue
    }
    if(-not (Test-PositiveRequirement $records $requirement))
    {
      $matchKind = if($requirement.ContainsKey('Match')) { $requirement.Match } else { 'Exact' }
      $failures += ("{0}: required {1} was not found with {2} value '{3}' from source /{4}/." -f
        $artifact.Name, $requirement.Label, $matchKind, $requirement.Value, $requirement.Source)
    }
  }
}

$failures = @($failures | Sort-Object -Unique)
if($failures.Count -gt 0)
{
  Write-Error ("RenderDic artifact audit failed with {0} finding(s):`n - {1}" -f `
      $failures.Count, ($failures -join "`n - "))
  exit 1
}

Write-Host "RenderDic artifact audit passed for $($artifacts.Count) artifact(s)."
exit 0
