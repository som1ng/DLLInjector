# Windows DLL Doctor for Pi

Pi 原生 Skill 包，用于只读 DLL/PE、依赖、进程模块和已保存报告诊断。
内容来自仓库唯一的 `windows-dll-doctor` Skill 源，通过构建复制；不维护另一份 Skill。

当前版本 `0.1.0-dev.1`，拟用包名 `@som1ng/dll-doctor-pi`。这是私有、未发布的包，
可从本地目录安装；仓库的项目级许可尚待确定。

## 从源码安装

需要 Windows、Node.js 22.19+、已安装的 Pi，以及可信的 DLLInjector CLI。
本轮验证 Pi `1.4.0`、CLI `2.0.0-dev.9` / Schema `1.6`。
Skill 保留其既有版本说明，并通过实际 `--version` / `--help` 核对可用能力。

先在源码中的 `integrations/pi` 目录执行：

```powershell
npm run build
```

此构建没有 npm 依赖，无需 `npm install`。然后进入希望使用诊断功能的项目，
将下面路径替换为实际绝对路径：

```powershell
$env:DLLINJECTOR_CLI = 'D:\src\DLLInjector\build\msvc-x64\Release\cli\dllinjector.exe'
& $env:DLLINJECTOR_CLI --version --json

pi install --local 'D:\src\DLLInjector\integrations\pi'
pi list
pi
```

Pi 项目信任流程仍然适用；确认你信任这个项目及该包之后，在 Pi 中使用
`/skill:windows-dll-doctor`，并给出需要分析的文件或 PID。
省略 `--local` 会写入 Pi 的用户级包配置，适用于多个项目。

本地包安装是对目录的引用，Pi 不会复制目录。请保留该目录；源码 Skill 更新后重新执行
`npm run build`，再重启 Pi 或 `/reload`。重复安装同一路径应只有一条包配置。
`DLLINJECTOR_CLI` 在上述 PowerShell 会话中设置，不修改系统 PATH；后续新会话需要再次设置，
也可在请求中提供可信 CLI 的绝对路径。

## 打包和离线使用

从源码包目录执行 `npm pack`，会先构建，再生成本地 `.tgz`。
归档包含包元数据、本说明和完整 Skill，不包含依赖目录、CLI 二进制、报告或测试脚本。
解压后将其中的 `package` 目录放到稳定位置，再通过 `pi install --local <绝对目录>` 安装。
已生成的包不需要原仓库即可被 Pi 加载；仍需单独准备可信的 Windows CLI。
这些本地安装命令不下载或发布 npm 包，也不启动 MCP 服务。

## 卸载

在安装时的项目目录执行，路径与安装时一致：

```powershell
pi remove --local 'D:\src\DLLInjector\integrations\pi'
```

用户级安装省略 `--local`。移除包引用后，重启 Pi 或 `/reload`。
Pi 不会删除本地包源目录、CLI 或诊断报告；其他包与配置应保留。

## 开发验证

在源码 `integrations/pi` 中执行 `npm test`，需要 Python 和 npm，检查构建与实际归档内容。
`npm.cmd run verify-host -- --pi-package <已安装的 Pi 包目录>` 使用已安装 Pi 的原生命令，
在临时配置中验证用户/项目安装、重复安装、信任与资源过滤、发现和卸载。
验证不会修改你的日常 Pi 配置，不创建模型会话，也不调用模型 API。
Windows PowerShell 下使用 `npm.cmd` 可避免 npm 的 PowerShell 包装脚本吞掉参数分隔符。
