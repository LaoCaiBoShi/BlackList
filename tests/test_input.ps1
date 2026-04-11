Write-Host "Testing Blacklist Checker with input redirection..." -ForegroundColor Green

# 使用输入重定向运行程序
Get-Content ".\test_input.txt" | .\x64\Release\BlacklistChecker.exe

Write-Host "\nTest completed." -ForegroundColor Green