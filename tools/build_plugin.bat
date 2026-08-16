@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
"C:\Program Files\CMake\bin\cmake.exe" --build "C:\Users\code\Documents\Default Project\buildDrum" --config Debug --target MixAgent_VST3 2>&1
"C:\Program Files\CMake\bin\cmake.exe" --build "C:\Users\code\Documents\Default Project\buildDrum" --config Debug --target MixAgent_Standalone 2>&1