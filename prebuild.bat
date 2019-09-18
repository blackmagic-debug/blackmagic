@echo off
setlocal enableDelayedExpansion
set preFix=#define FIRMWARE_VERSION
for /f "delims=" %%a in ('git describe --always --dirty') do @set "theRest=%%a"
set result=%prefix% "%theRest%"

echo %result%
