Write-Host "Testing Blacklist Checker on Windows..." -ForegroundColor Green
Write-Host "==========================================="
Write-Host "Running BlacklistChecker.exe..."
Write-Host "==========================================="

# 运行程序并自动提供输入
$process = Start-Process -FilePath ".\x64\Release\BlacklistChecker.exe" -NoNewWindow -PassThru -RedirectStandardInput ".\test_input.txt" -RedirectStandardOutput ".\test_output.txt"

# 等待程序执行完成
$process.WaitForExit()

# 显示输出
Write-Host "\nProgram Output:" -ForegroundColor Green
Get-Content ".\test_output.txt"

# 清理临时文件
Remove-Item ".\test_input.txt" -ErrorAction SilentlyContinue
Remove-Item ".\test_output.txt" -ErrorAction SilentlyContinue

Write-Host "\nTest completed." -ForegroundColor Green