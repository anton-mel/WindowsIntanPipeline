@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cl /EHsc main.cpp okFrontPanelDLL.cpp rhd2000evalboardusb3.cpp rhd2000registersusb3.cpp rhd2000datablockusb3.cpp /Fe:RHD2000Usb3Control.exe
pause
