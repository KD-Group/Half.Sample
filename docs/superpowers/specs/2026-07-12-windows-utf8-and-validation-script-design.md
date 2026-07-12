# Windows UTF-8 与统一验证脚本设计

## 问题

DAQ 程序输出 UTF-8 字节，但中文 Windows 控制台可能仍使用代码页 936，导致直接执行 `--help` 时中文乱码。统一执行函数已经写在 README 中，但没有独立脚本，现场人员必须手工复制函数。

## 目标

- Windows standalone 程序在连接控制台时将输出代码页设置为 UTF-8。
- 重定向和子进程捕获继续输出 UTF-8，不改变末行 JSON 契约。
- 提供可 dot-source 的 `scripts/daq_validation.ps1`，导出 `Invoke-DaqValidation`。
- README 改为加载脚本，不再维护重复函数实现。
- dot-source 时明确提示函数已加载；调用时默认显示命令、退出码和判定过程。
- 提供 `-Quiet`，自动化调用可关闭过程输出且仍只从 pipeline 得到 JSON payload。
- 同一脚本提供首选的直接执行入口：`-Variant mock|legacy|xnavi`、`-Config`、可选 `-OutputDir`，以及恰好一个 `-All/-Case/-From`。
- 直接执行自动选择 `cpp_build/daq_capability_test_<variant>.exe` 并断言 suite 成功契约；dot-source 仍只注册底层函数。

## 非目标

- 不改变 JSON schema、退出码或 stderr/stdout 分离规则。
- 不修改系统全局代码页，也不要求管理员权限。
- 不改用宽字符 stdout 模式，避免破坏重定向和 Python UTF-8 捕获。

## 实现

在 `main.cpp` 的 Windows standalone 入口调用一个小型初始化函数；仅调用 `SetConsoleOutputCP(CP_UTF8)`，失败不阻止命令执行。非 Windows 构建为空操作。

PowerShell 脚本保留 README 已审查过的全部断言：只捕获 stdout、立即保存退出码、解析最后非空 JSON 行，并检查顶层结果、case、failed case、策略和 evidence。README 使用：

```powershell
. .\scripts\daq_validation.ps1
```

现场首选直接执行，例如：

```powershell
.\scripts\daq_validation.ps1 -Variant xnavi -Config src\daq_capability_test\field_test_matrix.tsv -All
```

无参数直接执行显示中文帮助并退出 0；参数、配置文件或目标程序错误以非零退出。直接执行的 stdout 最后一行继续是单行 JSON。

过程信息使用 `Write-Host`，不能进入 success pipeline。正常流程依次显示 `RUN`、`EXIT`、`PASS`、`MESSAGE` 和 `EVIDENCE`；断言或 JSON 解析失败显示 `FAIL`、`MESSAGE`、`EVIDENCE` 以及每条 `ASSERT`，随后抛出包含 `exit`、`code`、`message`、`evidence`、`validation` 的异常。`-Quiet` 只关闭函数调用过程，不改变 dot-source 加载提示、返回对象、断言或异常。

## 验收

- 当前 PowerShell 不执行 `chcp 65001`，直接运行三个 DAQ 程序 `--help` 均显示可识别中文并退出 0。
- Python 以 UTF-8 捕获帮助仍通过。
- dot-source 脚本后，mock 成功和预期失败命令都能正确自动判定。
- 默认输出完整成功/失败过程，`-Quiet` 隐藏调用过程，二者 pipeline 都只返回 payload。
- JSON 解析失败也输出断言原因并抛出完整字段异常。
- 直接入口覆盖 Mock 成功、`-Case/-From`、scope 互斥、帮助、`-Quiet`、缺失参数/文件及 dot-source 兼容。
- 全量测试、构建和 `git diff --check` 通过。
