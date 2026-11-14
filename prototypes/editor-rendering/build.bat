@echo off
rem PROTOTYPE: builds both editor variants (Release). Output under build\*_artefacts\Release\
cd /d "%~dp0"
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 || exit /b 1
cmake --build build --config Release --target ForteWebProto_All ForteNativeProto_All -- /m || exit /b 1
echo.
echo Built. Standalone apps and VST3 bundles are in:
echo   build\ForteWebProto_artefacts\Release\
echo   build\ForteNativeProto_artefacts\Release\
