@echo off
"C:/Users/emmer/AppData/Local/Python/pythoncore-3.14-64/python.exe" "%~dp0llvm-lit.py" %*
exit /b %errorlevel%
