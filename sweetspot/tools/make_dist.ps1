# SWEET SPOT — 配る形を作る（Windows）。make dist から呼ばれる。売る版（sweetspot_sale.exe）が先にできている前提
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
$out = "dist\SweetSpot"
if (Test-Path "dist") { Remove-Item -Recurse -Force "dist" }
New-Item -ItemType Directory -Force -Path "$out\sounds", "$out\fonts", "$out\models" | Out-Null
Copy-Item "sweetspot_sale.exe" "$out\SweetSpot.exe"
# 音：選手の声のファイル名は番号に（売る版は v01〜v16 の名前で読む）
$names = @("federerr","djokovik","nadar","murraye","alcarazz","sinnerr","medvedeb","kirgios","williamz","swiontek","sabalenco","gauph","sharapowa","bartie","nishikoli","zverew")
foreach ($f in Get-ChildItem "sounds" -File | Where-Object { $_.Extension -in ".wav", ".ogg" }) {
    $dest = $f.Name
    for ($i = 0; $i -lt $names.Count; $i++) {
        if ($f.Name.StartsWith($names[$i] + "_")) { $dest = ("v{0:D2}_" -f ($i + 1)) + $f.Name.Substring($names[$i].Length + 1); break }
    }
    Copy-Item $f.FullName "$out\sounds\$dest"
}
Copy-Item "fonts\*" "$out\fonts\"
Copy-Item "models\player.glb", "models\palette.png", "models\players.txt" "$out\models\"
New-Item -ItemType Directory -Force -Path "$out\models\faces" | Out-Null
Copy-Item "models\faces\*.png" "$out\models\faces\"
New-Item -ItemType Directory -Force -Path "$out\models\players" | Out-Null
Copy-Item "models\players\*.glb" "$out\models\players\"
New-Item -ItemType Directory -Force -Path "$out\models\players\faces" | Out-Null
Copy-Item "models\players\faces\*.png" "$out\models\players\faces\"
Copy-Item "dist_files\CREDITS.txt", "dist_files\README_JA.txt", "dist_files\README_EN.txt" "$out\"
Compress-Archive -Path $out -DestinationPath "dist\SweetSpot_Windows.zip" -Force
Write-Host "dist\SweetSpot_Windows.zip"
