[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $WatchArguments
)

& (Join-Path $PSScriptRoot 'tools/mapgyver/ffmpeg/with-ffmpeg.ps1') xtask watch @WatchArguments
exit $LASTEXITCODE
