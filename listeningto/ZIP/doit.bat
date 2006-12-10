@echo off

rem Batch file to build and upload files
rem 
rem TODO: Integration with FL

set name=listeningto

rem To upload, this var must be set here or in other batch
rem set ftp=ftp://<user>:<password>@<ftp>/<path>

echo Building %name% ...

msdev ..\%name%.dsp /MAKE "%name% - Win32 Release" /REBUILD
msdev ..\%name%.dsp /MAKE "%name% - Win32 Unicode Release" /REBUILD
msdev ..\players\mlt_winamp\mlt_winamp.dsp /MAKE "mlt_winamp - Win32 Release" /REBUILD

echo Generating files for %name% ...

del *.zip
del *.dll
copy "..\..\..\bin\release\Plugins\%name%.dll"
copy "..\..\..\bin\release unicode\Plugins\%name%W.dll"
copy ..\Docs\%name%_changelog.txt
copy ..\Docs\%name%_version.txt
mkdir Docs
cd Docs
del /Q *.*
copy ..\..\Docs\langpack_%name%.txt
copy ..\..\m_%name%.h
cd ..
mkdir Plugins
cd Plugins
mkdir listeningto
cd listeningto
copy "..\..\..\..\..\bin\release\Plugins\listeningto\*.dll"
copy ..\..\..\Docs\readme_players.txt
cd ..
cd ..
mkdir src
cd src
del /Q *.*
copy ..\..\*.h
copy ..\..\*.cpp
copy ..\..\*.
copy ..\..\*.rc
copy ..\..\*.dsp
copy ..\..\*.dsw
mkdir Docs
cd Docs
del /Q *.*
copy ..\..\..\Docs\*.*
cd ..
mkdir sdk
cd sdk
del /Q *.*
copy ..\..\..\sdk\*.*
cd ..
cd ..

"C:\Program Files\Filzip\Filzip.exe" -a -rp %name%.zip %name%.dll Docs Plugins
"C:\Program Files\Filzip\Filzip.exe" -a -rp %name%W.zip %name%W.dll Docs Plugins
"C:\Program Files\Filzip\Filzip.exe" -a -rp %name%_src.zip src

del *.dll
cd Docs
del /Q *.*
cd ..
rmdir Docs
cd Plugins
cd listeningto
del /Q *.*
cd ..
rmdir listeningto
del /Q *.*
cd ..
rmdir Plugins
cd src
del /Q *.*
cd Docs
del /Q *.*
cd ..
rmdir Docs
cd sdk
del /Q *.*
cd ..
rmdir sdk
cd ..
rmdir src

if "%ftp%"=="" GOTO END

echo Going to upload files...
pause

"C:\Program Files\FileZilla\FileZilla.exe" -u .\%name%.zip %ftp% -overwrite -close 
"C:\Program Files\FileZilla\FileZilla.exe" -u .\%name%W.zip %ftp% -overwrite -close 
"C:\Program Files\FileZilla\FileZilla.exe" -u .\%name%_changelog.txt %ftp% -overwrite -close 
"C:\Program Files\FileZilla\FileZilla.exe" -u .\%name%_version.txt %ftp% -overwrite -close 

:END

echo Done.
