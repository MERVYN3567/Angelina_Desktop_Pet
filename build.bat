@echo off
setlocal
set "ROOT=%~dp0"

where windres >nul 2>nul || (
  echo windres was not found. Install MinGW-w64 and add its bin directory to PATH.
  exit /b 1
)
where g++ >nul 2>nul || (
  echo g++ was not found. Install MinGW-w64 and add its bin directory to PATH.
  exit /b 1
)

windres --output-format=coff -i "%ROOT%gif_pet.rc" -o "%ROOT%gif_pet_res.o"
if errorlevel 1 exit /b 1

g++ -municode -mwindows -O2 -std=c++17 -o "%ROOT%AngelinaPaperplanePet.exe" "%ROOT%gif_pet.cpp" "%ROOT%gif_pet_res.o" -lgdiplus -lole32
if errorlevel 1 exit /b 1

del "%ROOT%gif_pet_res.o" 2>nul
echo Built: AngelinaPaperplanePet.exe
