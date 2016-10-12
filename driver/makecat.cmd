REM inf2cat comes with the Windows Device Driver Kit (WDK).
REM See https://go.microsoft.com/fwlink/p/?LinkId=526733
set DIR=%~dp0
inf2cat /v /driver:%DIR% /os:10_X86 
signtool sign /debug /f "%MYKEYFILE%" /n "Chris Lovett" /t "http://timestamp.comodoca.com" /p "%MYKEYPSWD%" /fd sha256 blackmagic.cat 
signtool sign /debug /f "%MYKEYFILE%" /n "Chris Lovett" /t "http://timestamp.comodoca.com" /p "%MYKEYPSWD%" /fd sha256 blackmagic_upgrade.cat 
