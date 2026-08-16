param([string]$Root = "Assets")

$ErrorActionPreference = "Stop"
$base = Join-Path (Get-Location) $Root
$sounds = Join-Path $base "Sounds"
$zips = Join-Path $sounds "source_zips"
$credits = Join-Path $base "Credits"
$staging = Join-Path $sounds "_staging"

if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Force -Path $staging | Out-Null

# --- map source zips to category keywords ---
$rules = @(
  @{ zip = $null; src = "garebear-808-kit.zip"; cat = "808"; note = "parse_garebear"; keep = "*.wav" },
  @{ zip = $null; src = "garebear-drum-kit.zip"; cat = "";     note = "classify_by_folder"; keep = "*.wav" },
  @{ zip = $null; src = "bpm150-trap-loop.wav"; cat = "Loops"; note = "single_file"; keep = "*.wav" }
)

function Get-Stem([string]$p) { return [System.IO.Path]::GetFileNameWithoutExtension($p) }

# extract kits
foreach ($r in $rules) {
  $src = Join-Path $zips $r.src
  if (-not (Test-Path $src)) { continue }
  $baseName = [System.IO.Path]::GetFileNameWithoutExtension($r.src)
  if ($src.EndsWith(".zip")) {
    $dest = Join-Path $staging $baseName
    Expand-Archive -LiteralPath $src -DestinationPath $dest -Force
  } else {
    Copy-Item $src (Join-Path $staging $baseName)
  }
}

# --- classify drum kit by folder name heuristics ---
$catMap = @{ kick="Kicks"; kick2="Kicks"; kicks="Kicks"; snare="Snares"; snares="Snares";
             hat="Hats"; hats="Hats"; hh="Hats"; hihat="Hats"; hi-hat="Hats"; "hihats"="Hats";
             clap="Claps"; claps="Claps"; cymb="Cymbals"; cymbal="Cymbals"; cymbals="Cymbals";
             perc="Percs"; percs="Percs"; fx="FX"; tom="Toms"; toms="Toms";
             bell="Bells"; bells="Bells"; cowbell="Percs"; block="Percs"; rim="Percs" }

function Get-CategoryForFolder([string]$name) {
  $n = $name.ToLower()
  foreach ($k in $catMap.Keys) { if ($n -like "*$k*") { return $catMap[$k] } }
  if ($n -match "^(0-9+|v[0-9]+|\d+)$") { return "" }   # velocity dirs
  return ""
}

$manifest = New-Object System.Collections.ArrayList
$seen = @{}   # sha256 -> first id (dedupe)
$idCounter = 0
$allWavs = Get-ChildItem $staging -Recurse -Filter *.wav | Where-Object { $_.FullName -notlike "*source_zips*" }

foreach ($f in $allWavs) {
  $rel = $f.FullName.Substring($staging.Length).TrimStart('\','/')
  $leaf = Get-Stem $f.Name
  $parent = $f.Directory.Name

  $category = ""
  if ($f.FullName -like "*Free-808*") {
    $category = "808"
    # GareBear 808: filename encodes note, e.g. 808_FFs_wav or similar; try to read root note
  } elseif ($f.FullName -like "*garebear-drum-kit*") {
    $category = Get-CategoryForFolder $parent
    if (-not $category) { $category = (Get-CategoryForFolder $leaf) }
    if (-not $category) { $category = (Get-CategoryForFolder $f.Directory.Parent.Name) }
  } elseif ($f.Name -like "bpm150-trap-loop*") {
    $category = "Loops"
  }
  if (-not $category) { $category = "Percs" }  # fallback

  # root note guess from 808 kit folder/name
  $rootNote = $null
  if ($category -eq "808") {
    $rootNote = if ($leaf -match "C2") { $null } else { $null }
    if ($leaf -match "[A-G]b?[0-9]") { $matches[0]; }
  }

  # license source attribution
  $licenseFile = ""
  if ($f.FullName -like "*Free-808*")   { $licenseFile = "garebear99-808.txt" }
  elseif ($f.FullName -like "*garebear-drum-kit*") { $licenseFile = "garebear99-drums.txt" }
  elseif ($f.Name -like "bpm150*")       { $licenseFile = "archive-pd-cc0.txt" }

  $entry = [ordered]@{
    id = ""
    file = ""
    category = $category
    rootNote = $rootNote
    license = $licenseFile
    credit = @()
  }
  $null = $manifest.Add([pscustomobject]$entry)
}

# outputs
Write-Host ("staged files: " + $allWavs.Count)
Get-ChildItem $staging -Directory | ForEach-Object { Write-Host ("dir: " + $_.Name) }
Get-ChildItem $staging -Recurse -File | Group-Object Extension | ForEach-Object { Write-Host ("ext: " + $_.Name + " x" + $_.Count) }