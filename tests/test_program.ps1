Write-Host "Testing Blacklist Checker..." -ForegroundColor Green

# 运行程序并捕获输出
$output = @(
    "D:\Coder\socket\BlackList\20230620_44_1.json",
    "44011527223202919285",
    "44011527223202919286",
    "quit"
) | & ".\x64\Release\BlacklistChecker.exe"

# 显示输出
Write-Host "Program Output:" -ForegroundColor Green
$output

Write-Host "\nTest completed." -ForegroundColor Green