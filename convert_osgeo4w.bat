@echo off
REM ===================================================================
REM FGD-DEM to GeoTIFF Converter (OSGeo4W Edition)
REM ===================================================================
REM
REM Usage:
REM   convert.bat input.zip output.tif
REM
REM Example:
REM   convert.bat FG-GML-5339-45-DEM10B-20250101.zip output.tif
REM
REM Requirements:
REM   - OSGeo4W must be installed
REM   - C:\OSGeo4W64\bin must be in PATH
REM

setlocal

REM Check if OSGeo4W is installed and in PATH
where gdal_translate >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: OSGeo4W not found in PATH!
    echo.
    echo Please install OSGeo4W and add C:\OSGeo4W64\bin to your PATH.
    echo Or run this from OSGeo4W Shell.
    echo.
    echo Download OSGeo4W from: https://trac.osgeo.org/osgeo4w/
    pause
    exit /b 1
)

REM Get the directory where this batch file is located
set "SCRIPT_DIR=%~dp0"

REM Run the converter
"%SCRIPT_DIR%convert_fgd_dem_cpp.exe" %*

REM Check if successful
if %ERRORLEVEL% EQU 0 (
    echo.
    echo Conversion completed successfully!
) else (
    echo.
    echo Conversion failed with error code: %ERRORLEVEL%
    pause
    exit /b %ERRORLEVEL%
)

endlocal
