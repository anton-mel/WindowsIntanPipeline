@echo off
echo Building Windows dual-output neural data acquisition system...
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cl /EHsc main_windows_dual.cpp okFrontPanelDLL.cpp rhd2000evalboardusb3.cpp rhd2000registersusb3.cpp rhd2000datablockusb3.cpp /Fe:IntanDualOutput.exe
if %ERRORLEVEL% == 0 (
    echo.
    echo Build successful! Executable: IntanDualOutput.exe
    echo.
    echo This program will:
    echo  1. Acquire neural data from Intan device
    echo  2. Save data to timestamped .dat files
    echo  3. Send data to FPGA via Python pipe
    echo  4. Stream data to visualizer via Windows shared memory
    echo.
) else (
    echo Build failed!
)
pause
