@echo off
echo Testing Blacklist Checker with input redirection...
echo =

rem 使用输入重定向运行程序
x64\Release\BlacklistChecker.exe < test_input.txt

echo =
echo Test completed.
pause