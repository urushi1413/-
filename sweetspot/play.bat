@echo off
rem SWEET SPOT：最新版に更新してから起動する（ダブルクリックするだけ）
cd /d "%~dp0"
git pull
start "" "%~dp0play_windows.exe"
