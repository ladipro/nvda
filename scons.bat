@echo off
rem We need this script because .py probably isn't in pathext.
rem We can't just call python -c because it may not be in the path.
rem Instead, find the python launcher (installed by python 3)
where py >nul&& (
	rem Python launcher is present in the PATH
	rem Call python 2.7 for 32 bits
	py -2.7-32 "%~dp0\scons.py" %*
) || (
	rem Python registers itself with the .py extension, so call scons.py.
	"%~dp0\scons.py" %*
)
