# seachart for Windows: sets up Python on first start and offers a menu.
#
#   seachart.bat                 -> menu
#   seachart.bat <arguments>     -> runs "seachart <arguments>"
#
# If seachart.exe (built by .github/workflows/seachart-windows.yml) lies
# next to this script, it is used and no Python installation is needed.

param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$env:PYTHONIOENCODING = 'utf-8'

$Here    = Split-Path -Parent $MyInvocation.MyCommand.Path
$Tool    = Split-Path -Parent $Here                     # tools\seachart
$Home2   = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'seachart'
$Db      = Join-Path $Home2 'seachart.db'
$Charts  = Join-Path $Home2 'charts'
$Venv    = Join-Path $Home2 'python'
$Exe     = Join-Path $Here 'seachart.exe'
New-Item -ItemType Directory -Force -Path $Home2, $Charts | Out-Null

function Find-Python {
    $candidates = @(@{ Exe = 'py'; Args = @('-3') }, @{ Exe = 'python'; Args = @() })
    foreach ($c in $candidates) {
        if (Get-Command $c.Exe -ErrorAction SilentlyContinue) {
            $check = $c.Args + @('-c', 'import sys; print(sys.version_info >= (3, 9))')
            $ver = & $c.Exe @check 2>$null
            if ($ver -eq 'True') { return $c }
        }
    }
    return $null
}

function Initialize-Python {
    $py = Join-Path $Venv 'Scripts\python.exe'
    if (Test-Path $py) { return $py }
    $base = Find-Python
    if (-not $base) {
        Write-Host 'Python 3 wurde nicht gefunden.' -ForegroundColor Yellow
        if (Get-Command winget -ErrorAction SilentlyContinue) {
            $a = Read-Host 'Jetzt mit winget installieren? (j/n)'
            if ($a -eq 'j') {
                winget install -e --id Python.Python.3.12 --scope user
                $env:Path = [Environment]::GetEnvironmentVariable('Path', 'User') + ';' + $env:Path
                $base = Find-Python
            }
        }
        if (-not $base) {
            throw 'Bitte Python 3.9+ von https://www.python.org installieren und erneut starten.'
        }
    }
    Write-Host 'Richte Python-Umgebung ein (einmalig) ...'
    $venvArgs = $base.Args + @('-m', 'venv', $Venv)
    & $base.Exe @venvArgs
    & $py -m pip install --upgrade pip | Out-Null
    & $py -m pip install numpy shapely osmium
    if ($LASTEXITCODE -ne 0) { throw 'pip install fehlgeschlagen' }
    return $py
}

function Invoke-Seachart([string[]]$Arguments) {
    $all = @('--db', $Db) + $Arguments
    if (Test-Path $Exe) {
        & $Exe @all
    } else {
        $py = Initialize-Python
        $env:PYTHONPATH = $Tool
        & $py -m seachart @all
    }
    if ($LASTEXITCODE -ne 0) {
        Write-Host "seachart beendet mit Fehler $LASTEXITCODE" -ForegroundColor Red
    }
}

function Select-File([string]$Filter) {
    Add-Type -AssemblyName System.Windows.Forms
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Filter = $Filter
    $dlg.InitialDirectory = [Environment]::GetFolderPath('MyDocuments')
    if ($dlg.ShowDialog() -eq 'OK') { return $dlg.FileName }
    return $null
}

function Read-Value([string]$Prompt, [string]$Default = '') {
    $v = Read-Host ($(if ($Default) { "$Prompt [$Default]" } else { $Prompt }))
    if ($v) { return $v } else { return $Default }
}

function Add-Optional([System.Collections.ArrayList]$List, [string]$Flag, [string]$Value) {
    if ($Value) { [void]$List.Add($Flag); [void]$List.Add($Value) }
}

if ($Rest) {
    Invoke-Seachart $Rest
    exit $LASTEXITCODE
}

while ($true) {
    Write-Host ''
    Write-Host '=== seachart - OpenSeaMap offline fuer OpenCPN ===' -ForegroundColor Cyan
    Write-Host "Daten: $Db"
    Write-Host ' 1  Revier herunterladen (Rechteck)'
    Write-Host ' 2  Daten fuer eine Route herunterladen (GPX aus OpenCPN)'
    Write-Host ' 3  Karten fuer eine Route erstellen (offline)'
    Write-Host ' 4  Wetter aktualisieren'
    Write-Host ' 5  Haefen und Ankerplaetze anzeigen'
    Write-Host ' 6  Hafen/Ankerplatz hinzufuegen'
    Write-Host ' 7  Hafen/Ankerplatz bearbeiten'
    Write-Host ' 8  Foto zu Hafen/Ankerplatz hinzufuegen'
    Write-Host ' 9  Kartenordner oeffnen'
    Write-Host ' I  Was ist offline vorhanden?'
    Write-Host ' Q  Beenden'
    $choice = (Read-Host 'Auswahl').ToUpper()
    try {
        switch ($choice) {
            '1' {
                $bbox = Read-Value 'Rechteck W,S,E,N in Grad (z.B. 9.8,53.9,13.0,55.6)'
                if ($bbox) { Invoke-Seachart @('download', '--bbox', $bbox) }
            }
            '2' {
                $gpx = Select-File 'GPX (*.gpx)|*.gpx'
                if ($gpx) {
                    $w = Read-Value 'Korridor +/- sm' '10'
                    Invoke-Seachart @('download', $gpx, '--width', $w)
                }
            }
            '3' {
                $gpx = Select-File 'GPX (*.gpx)|*.gpx'
                if ($gpx) {
                    $w = Read-Value 'Korridor +/- sm' '8'
                    $s = Read-Value 'Massstab 1:' '50000'
                    Invoke-Seachart @('route', $gpx, '--width', $w, '--scale', $s, '--out', $Charts)
                    Write-Host "In OpenCPN: Optionen > Karten > Verzeichnis hinzufuegen: $Charts"
                }
            }
            '4' { Invoke-Seachart @('weather', '--days', (Read-Value 'Tage' '7')) }
            '5' { Invoke-Seachart @('harbours', 'list') }
            '6' {
                $a = [System.Collections.ArrayList]@('harbours', 'add')
                Add-Optional $a '--kind' (Read-Value 'Art (marina, harbour, anchorage, buoy)' 'anchorage')
                Add-Optional $a '--name' (Read-Value 'Name')
                Add-Optional $a '--lat' (Read-Value 'Breite (z.B. 54.4123)')
                Add-Optional $a '--lon' (Read-Value 'Laenge (z.B. 10.1825)')
                Add-Optional $a '--notes' (Read-Value 'Notizen')
                Add-Optional $a '--depth' (Read-Value 'Wassertiefe m')
                Invoke-Seachart $a
            }
            '7' {
                $id = Read-Value 'Nummer (siehe Liste)'
                if ($id) {
                    $a = [System.Collections.ArrayList]@('harbours', 'edit', $id)
                    Write-Host 'Leer lassen = unveraendert'
                    Add-Optional $a '--name' (Read-Value 'Name')
                    Add-Optional $a '--notes' (Read-Value 'Notizen')
                    Add-Optional $a '--depth' (Read-Value 'Wassertiefe m')
                    Add-Optional $a '--berths' (Read-Value 'Liegeplaetze')
                    Add-Optional $a '--vhf' (Read-Value 'UKW-Kanal')
                    Add-Optional $a '--phone' (Read-Value 'Telefon')
                    Add-Optional $a '--website' (Read-Value 'Website')
                    Add-Optional $a '--facilities' (Read-Value 'Ausstattung (fuel,water,electricity,showers,...)')
                    Add-Optional $a '--rating' (Read-Value 'Bewertung 1-5')
                    Invoke-Seachart $a
                }
            }
            '8' {
                $id = Read-Value 'Nummer (siehe Liste)'
                $img = Select-File 'Bilder (*.jpg;*.jpeg;*.png)|*.jpg;*.jpeg;*.png'
                if ($id -and $img) {
                    Invoke-Seachart @('harbours', 'photo', $id, $img, '--caption', (Read-Value 'Bildunterschrift'))
                }
            }
            '9' { Start-Process explorer.exe $Charts }
            'I' { Invoke-Seachart @('info') }
            'Q' { exit 0 }
            default { Write-Host 'Unbekannte Auswahl' }
        }
    } catch {
        Write-Host $_ -ForegroundColor Red
    }
}
