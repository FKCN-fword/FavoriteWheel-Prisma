param([string]$CommonLibSource)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force extern | Out-Null
    if (!(Test-Path 'extern/CommonLibVR/xmake.lua')) {
        if (!$CommonLibSource) {
            $revision = '94faaed0c60eddd8347767f2d4d29a97c93bde8c'
            $stage = Join-Path $projectRoot ('build/bootstrap-commonlib-' + [Guid]::NewGuid().ToString('N'))
            New-Item -ItemType Directory -Force $stage | Out-Null
            $archive = Join-Path $stage 'upstream.zip'
            Invoke-WebRequest -Uri "https://codeload.github.com/alandtse/CommonLibSSE-NG/zip/$revision" -OutFile $archive
            if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne '0F5B98D89BA0DC20A2596156E9A37D70E04FDD311ECDA2C6CD60773B93CD3ADA') {
                throw 'Unexpected CommonLibSSE-NG source archive hash.'
            }
            Expand-Archive -LiteralPath $archive -DestinationPath $stage
            $CommonLibSource = Join-Path $stage "CommonLibSSE-NG-$revision"
            Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/CommonLibSSE-NG-REVISION.txt') -Destination (Join-Path $CommonLibSource 'UPSTREAM_REVISION.txt')
        }
        $manifest = Join-Path $CommonLibSource 'UPSTREAM_REVISION.txt'
        if (!(Test-Path (Join-Path $CommonLibSource 'xmake.lua')) -or
            !(Test-Path $manifest) -or
            !(Get-Content -LiteralPath $manifest -Raw).Contains('94faaed0c60eddd8347767f2d4d29a97c93bde8c')) {
            throw 'CommonLibSource must be the pinned CommonLibSSE-NG v11.0.0 source snapshot.'
        }
        & robocopy $CommonLibSource 'extern/CommonLibVR' /E /XD .git .xmake build /NFL /NDL /NJH /NJS /NP
        if ($LASTEXITCODE -ge 8) { throw 'CommonLib source copy failed.' }
    }
    if (!(Test-Path 'extern/CommonLibVR/src/REL/IDDB.cpp') -or
        !(Get-Content -LiteralPath 'extern/CommonLibVR/include/SKSE/Version.h' -Raw).Contains('RUNTIME_SSE_1_7_104')) {
        throw 'Existing CommonLib snapshot predates 1.7 support. Replace it with the pinned v11.0.0 source first.'
    }
    Write-Output 'Dependencies ready. Run xmake f -m release -y, then xmake.'
} finally { Pop-Location }
