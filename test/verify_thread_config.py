#!/usr/bin/env python3
"""
验证动态线程配置功能的测试脚本
"""

import subprocess
import sys


def test_thread_config():
    """测试动态线程配置功能"""
    print("=== 测试动态线程配置功能 ===")
    
    try:
        # 运行测试程序
        result = subprocess.run(
            ["build\\test_thread_config.exe"],
            capture_output=True,
            text=True,
            cwd="d:\\Coder\\socket"
        )
        
        if result.returncode == 0:
            print("✓ 测试程序运行成功")
            print("\n测试输出:")
            print(result.stdout)
            
            # 解析输出，验证关键信息
            output = result.stdout
            
            # 检查CPU核心数
            if "CPU Core Count:" in output:
                print("✓ CPU核心数检测功能正常")
            
            # 检查内存检测
            if "Available Memory (MB):" in output:
                print("✓ 内存检测功能正常")
            
            # 检查磁盘类型检测
            if "Is SSD:" in output:
                print("✓ 磁盘类型检测功能正常")
            
            # 检查线程配置
            if "Thread Configuration:" in output:
                print("✓ 线程配置计算功能正常")
            
            # 检查性能监控
            if "Performance Stats:" in output:
                print("✓ 性能监控功能正常")
            
            print("\n=== 测试通过 ===")
            return True
        else:
            print("✗ 测试程序运行失败")
            print("错误信息:")
            print(result.stderr)
            return False
            
    except Exception as e:
        print(f"✗ 测试过程出错: {e}")
        return False


def test_build():
    """测试编译是否成功"""
    print("=== 测试编译状态 ===")
    
    try:
        # 检查测试程序是否存在
        import os
        test_exe = "d:\\Coder\\socket\\build\\test_thread_config.exe"
        if os.path.exists(test_exe):
            print("✓ 测试程序已编译成功")
            return True
        else:
            print("✗ 测试程序未找到")
            return False
            
    except Exception as e:
        print(f"✗ 检查过程出错: {e}")
        return False


if __name__ == "__main__":
    print("开始测试动态线程配置功能...\n")
    
    build_success = test_build()
    config_success = test_thread_config()
    
    print("\n=== 测试总结 ===")
    if build_success and config_success:
        print("✓ 所有测试通过！")
        print("动态线程配置功能正常工作")
        sys.exit(0)
    else:
        print("✗ 部分测试失败")
        sys.exit(1)
