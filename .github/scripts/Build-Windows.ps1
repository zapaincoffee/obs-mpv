[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Build-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "A 64-bit system is required to build the project."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The obs-studio PowerShell build script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Build {
    trap {
        Pop-Location -Stack BuildTemp -ErrorAction 'SilentlyContinue'
        Write-Error $_
        Log-Group
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach($Utility in $UtilityFunctions) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    # Download libmpv dev files
    $MpvUrl = "https://sourceforge.net/projects/mpv-player-windows/files/libmpv/mpv-dev-x86_64-20251214-git-f7be2ee.7z/download"
    $MpvZip = "$env:TEMP\mpv-dev.7z"
    $MpvDir = "$ProjectRoot\libmpv"
    
    Log-Group "Downloading libmpv..."
    # curl -L is much better at SourceForge redirects than Invoke-WebRequest
    & curl.exe -L -o $MpvZip $MpvUrl
    
    if (-not (Test-Path $MpvZip)) {
        Write-Error "Failed to download libmpv. File not found: $MpvZip"
        exit 1
    }
    
    $zipItem = Get-Item $MpvZip
    Write-Host "Downloaded libmpv size: $($zipItem.Length) bytes"

    Log-Group "Extracting libmpv..."
    if (-not (Test-Path $MpvDir)) { New-Item -ItemType Directory -Path $MpvDir | Out-Null }
    
    # Run 7z without suppressing output
    & 7z x $MpvZip -o"$MpvDir" -y
    
    Log-Group "Debugging libmpv content..."
    Get-ChildItem -Path $MpvDir
    
    # Configure CMake with MPV path
    $env:MPV_INCLUDE_DIRS = "$MpvDir\include"
    $env:MPV_LIBRARY_DIRS = "$MpvDir"
    $env:CMAKE_PREFIX_PATH = "$MpvDir"
    
    Write-Host "MPV_INCLUDE_DIRS: $env:MPV_INCLUDE_DIRS"
    Write-Host "MPV_LIBRARY_DIRS: $env:MPV_LIBRARY_DIRS"

    Push-Location -Stack BuildTemp
    Ensure-Location $ProjectRoot

    $CmakeArgs = @('--preset', "windows-ci-${Target}")
    $CmakeBuildArgs = @('--build')
    $CmakeInstallArgs = @()

    if ( $DebugPreference -eq 'Continue' ) {
        $CmakeArgs += ('--debug-output')
        $CmakeBuildArgs += ('--verbose')
        $CmakeInstallArgs += ('--verbose')
    }

    $CmakeBuildArgs += @(
        '--preset', "windows-${Target}"
        '--config', $Configuration
        '--parallel'
        '--', '/consoleLoggerParameters:Summary', '/noLogo'
    )

    $CmakeInstallArgs += @(
        '--install', "build_${Target}"
        '--prefix', "${ProjectRoot}/release/${Configuration}"
        '--config', $Configuration
    )

    Log-Group "Configuring ${ProductName}..."
    Invoke-External cmake @CmakeArgs

    Log-Group "Building ${ProductName}..."
    Invoke-External cmake @CmakeBuildArgs

    Log-Group "Installing ${ProductName}..."
    Invoke-External cmake @CmakeInstallArgs

    Pop-Location -Stack BuildTemp
    Log-Group
}

Build
