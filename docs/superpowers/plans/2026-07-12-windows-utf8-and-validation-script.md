# Windows UTF-8 And Validation Script Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复 Windows 控制台中文乱码，并把统一 DAQ 验证函数交付为独立 PowerShell 脚本。

**Architecture:** standalone 入口只负责设置 Windows 控制台输出代码页，CLI 仍输出 UTF-8 窄字节。PowerShell 函数从 README 提取到单一脚本，README 只记录加载与调用方式。

**Tech Stack:** C++14、Windows Console API、PowerShell、Python unittest、SCons

---

### Task 1: Windows 控制台 UTF-8

**Files:**
- Modify: `src/daq_capability_test/main.cpp`
- Modify: `tests/daq_capability_test/test_header_layout.py`

- [ ] 写失败测试，断言 standalone 入口包含受 `_WIN32` 保护的 `SetConsoleOutputCP(CP_UTF8)`，且不启用 `_O_U8TEXT`。
- [ ] 运行 `python -m unittest tests.daq_capability_test.test_header_layout -v`，确认测试先失败。
- [ ] 在 `main.cpp` 添加最小 Windows 控制台初始化。
- [ ] 构建 mock、legacy、XNavi，并在当前 PowerShell 直接运行 `--help`。

### Task 2: 独立验证脚本

**Files:**
- Create: `scripts/daq_validation.ps1`
- Modify: `README.md`
- Modify: `tests/daq_capability_test/test_mock_cli.py`

- [ ] 写失败测试，dot-source 脚本并验证函数存在、stdout/stderr 分离和成功/预期失败判定。
- [ ] 将 README 中已审查函数原样移动到脚本。
- [ ] README 改为 `. .\scripts\daq_validation.ps1`，保留参数和错误契约说明。
- [ ] 用 mock success 与 non-stationary 命令验证脚本。

### Task 3: 回归与提交

**Files:**
- Verify all changed files

- [ ] 运行 `python -m unittest discover -s tests -v`。
- [ ] 构建四个 DAQ 目标并运行 C++ unit tests。
- [ ] 运行 `git diff --check`，确认仅保留原有未跟踪文件。
- [ ] 提交为一个 focused commit。

### Task 4: 验证过程可见性

**Files:**
- Modify: `scripts/daq_validation.ps1`
- Modify: `README.md`
- Modify: `tests/daq_capability_test/test_mock_cli.py`

- [x] 先写 PowerShell 集成测试，覆盖加载提示、成功流程、失败断言、JSON 解析失败、`-Quiet`、pipeline 单对象和 stderr 分离。
- [x] 使用 `Write-Host` 输出过程，保证 success pipeline 仍只返回 payload。
- [x] README 记录默认输出和 `-Quiet` 的用途。
- [x] 运行 focused 测试、全量测试和四个 SCons 目标。

### Task 5: 直接执行入口

**Files:**
- Modify: `scripts/daq_validation.ps1`
- Modify: `README.md`
- Modify: `tests/daq_capability_test/test_mock_cli.py`

- [x] 先写真实 PowerShell 进程测试，确认入口缺失时失败。
- [x] 支持 variant、配置、输出目录、互斥 scope 和 quiet，自动选择构建目标并复用统一断言函数。
- [x] 无参数显示中文帮助；参数、文件和程序错误返回非零。
- [x] README 将直接执行设为首选，dot-source 函数降为高级用法。
