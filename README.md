# URL Alias Redirect (C++)

一个基于 `Host` 请求头匹配的轻量级 URL 别名跳转服务，使用 **C++17 + Winsock2 + OpenSSL** 实现，面向 Windows 本地 / 内网环境。

配置好别名后，在浏览器地址栏直接输入 `alias`（不需要 `/` 和端口），即可 302 跳转到对应地址。

- GitHub: https://github.com/PH-Code26/redirect-server-cpp
- Gitee: https://gitee.com/PH-Code26/redirect-server-cpp

## 功能特性

| 特性 | 说明 |
| --- | --- |
| Host 别名跳转 | 按请求的 `Host` 匹配 `aliases.json`，命中后返回 `302` |
| 配置热更新 | 每次请求检查文件修改时间，改完 `aliases.json` 无需重启 |
| HTTP / HTTPS 双端口 | 默认 `5666`（HTTP）、`5667`（HTTPS） |
| 管理页 | 访问未配置的别名时返回 `404` 页面，列出当前所有别名 |
| CORS 预检 | `OPTIONS` 请求返回 `204` 及宽松的 CORS 头 |
| 单文件可执行 | 优先静态链接 OpenSSL，`bin` 目录下无需额外 DLL |
| 一键安装 | 自动写入 hosts、配置端口转发、注册登录自启 |
| 静默运行 | 通过计划任务后台启动，日志写入 `log/service.log` |

## 工作原理

```text
浏览器请求 http://alias
        │  Host: alias
        ▼
127.0.0.2:80            ← hosts 文件写入的别名解析
        │  netsh portproxy 80 → 5666
        ▼
url-alias-redirect.exe  (:5666 / :5667)
        │  读取 config/aliases.json，匹配 Host
        ├── 命中 → 302 Found，Location: 目标地址
        ├── OPTIONS → 204 No Content（CORS）
        └── 未命中 → 404 HTML 列表页
```

`aliases.json` 中的值是纯 IP / 域名时，程序会自动补上 `http://`；已带 `://` 的完整 URL 则原样使用。

## 目录结构

```text
redirect-server-cpp/
├── src/
│   └── server.cpp            # 服务主程序（HTTP/HTTPS、路由、配置加载）
├── config/
│   └── aliases.json          # 别名映射配置
├── scripts/
│   ├── setup.bat             # 一键安装（管理员）
│   ├── generate-cert.ps1     # 生成自签名证书（PFX + PEM）
│   ├── start-silent.ps1      # 后台静默启动
│   └── https/
│       ├── https-on.bat      # 开启 443 → 5667 转发
│       └── https-off.bat     # 关闭 443 转发
├── build.bat                 # 编译脚本（MSVC + OpenSSL）
├── start.bat                 # 前台启动
├── restart.bat               # 重启服务（管理员）
├── cleanup.bat               # 卸载（管理员）
├── CMakeLists.txt            # CMake 构建入口
└── .gitignore
```

> `bin/`、`certs/`、`log/` 为运行时生成目录，已被 `.gitignore` 忽略。

## 环境要求

- Windows 10 / 11（x64）
- Visual Studio 2022 Build Tools（MSVC，支持 C++17）
- OpenSSL 3.x（`build.bat` 会尝试通过 `winget` 自动安装）
- 管理员权限（写入 hosts、设置端口转发、注册计划任务）

## 快速开始

### 1. 编译

```bat
build.bat
```

脚本会依次查找 `cl.exe`、定位或安装 OpenSSL，然后输出 `bin\url-alias-redirect.exe`。优先使用静态 OpenSSL；若只找到动态库，会自动把 `libssl-3-x64.dll` / `libcrypto-3-x64.dll` 复制到 `bin\`。

### 2. 配置别名

编辑 `config/aliases.json`：

```json
{
  "tmpoj.com": "121.43.34.166",
  "docs.local": "http://192.168.1.10:8080"
}
```

- 键：不带端口的域名 / 主机名（匹配时会转小写）
- 值：目标 IP、域名，或完整 URL
- 仅支持扁平的 `字符串 → 字符串` 映射

### 3. 安装并启动

以管理员身份运行：

```bat
scripts\setup.bat
```

脚本会完成四件事：

1. 调用 `generate-cert.ps1` 生成自签名证书
2. 把别名写入 hosts（带 `# === URL-ALIAS-START-CPP ===` 标记，便于清理），并刷新 DNS
3. 设置 `80 → 5666` 端口转发
4. 注册登录自启计划任务，并静默启动服务

### 4. 验证

- 打开 `http://localhost:5666` 或 `https://localhost:5667`
- 首次访问 HTTPS 时，在浏览器中接受自签名证书警告
- 之后直接输入别名即可，例如 `http://tmpoj.com`

如需启用 443 入口：

```bat
scripts\https\https-on.bat
```

## 配置参考

### 端口

| 用途 | 默认端口 | 环境变量 |
| --- | --- | --- |
| HTTP 监听 | 5666 | `HTTP_PORT` |
| HTTPS 监听 | 5667 | `HTTPS_PORT` |
| 外部 HTTP 入口 | 80（portproxy） | — |
| 外部 HTTPS 入口 | 443（portproxy） | — |

### 响应规则

| 请求情况 | 响应 |
| --- | --- |
| `Host` 命中别名 | `302 Found`，`Location` 为目标地址 |
| `OPTIONS` 请求 | `204 No Content`，允许跨域 |
| `Host` 未命中 | `404` HTML 页面，列出全部已配置别名 |

### 路径约定

程序以 **exe 所在目录的上级目录** 作为项目根目录，据此定位：

- `config/aliases.json`
- `certs/cert.pem`、`certs/key.pem`

因此请不要随意移动 `bin\url-alias-redirect.exe`；用 CMake 构建时，也需把产物放到 `bin\` 后再运行。

## HTTPS 说明

- 证书由 `scripts\generate-cert.ps1` 生成，使用当前 `aliases.json` 中的全部别名作为 SAN（DNS Name）。
- 新增别名后需要重新生成证书：先删除 `certs\cert.pfx`、`certs\cert.pem`、`certs\key.pem`，再运行一次 `setup.bat` 或 `generate-cert.ps1`。
- 若系统未安装 OpenSSL，脚本会尝试用 .NET 导出私钥；失败时 HTTPS 将不可用。
- `cert.pfx` 的默认密码为 `alias123`。手动转换 PEM 的命令：

```bat
openssl pkcs12 -in certs\cert.pfx -out certs\cert.pem -clcerts -nokeys -passin pass:alias123
openssl pkcs12 -in certs\cert.pfx -out certs\key.pem -nocerts -nodes -passin pass:alias123
```

- 缺少 `cert.pem` 或 `key.pem` 时，HTTP 服务仍会正常启动，仅禁用 HTTPS。

## 脚本速查

| 脚本 | 作用 | 是否需要管理员 |
| --- | --- | --- |
| `build.bat` | 编译到 `bin\` | 否 |
| `scripts\setup.bat` | 生成证书、写 hosts、端口转发、注册自启并启动 | 是 |
| `start.bat` | 前台启动服务 | 否 |
| `restart.bat` | 停止占用 5666/5667 的进程并静默重启 | 是 |
| `cleanup.bat` | 停止服务、删除自启、移除 80/443 转发、清理 hosts | 是 |
| `scripts\https\https-on.bat` | 添加 `443 → 5667` 转发 | 是 |
| `scripts\https\https-off.bat` | 删除 443 转发 | 是 |

## 卸载

```bat
cleanup.bat
```

会删除计划任务、端口转发和 hosts 中的别名段，但不会删除 `bin/`、`certs/`、`log/` 等目录，如需彻底清理请手动删除。

## 常见问题

**别名不生效**

- 确认 hosts 中已写入 `127.0.0.2  <alias>`，并执行过 `ipconfig /flushdns`
- 确认 `80 → 5666` 转发存在：`netsh interface portproxy show v4tov4`
- 确认访问的 `Host` 与 `aliases.json` 的键完全一致（键会转小写，但不要带端口）
- 直接访问 `http://localhost:5666` 能看到别名列表，说明服务本身正常

**端口被占用**

`restart.bat` 会结束占用 5666/5667 的进程；也可用 `netstat -ano | findstr :5666` 手动排查。80/443 被 IIS 等程序占用时，需要先释放端口。

**HTTPS 无法访问**

- 检查 `certs\cert.pem` 与 `certs\key.pem` 是否存在，缺失时程序会打印提示
- 首次访问必须手动接受自签名证书
- 浏览器提示证书与域名不匹配时，说明该别名不在证书 SAN 中，需重新生成证书

**修改配置要重启吗**

不需要。程序按文件修改时间自动重载；保存 `aliases.json` 后刷新页面即可。

## 技术实现

- 手写 HTTP/1.1 解析与响应（`winsock2` + 原生 socket，每个连接一个线程）
- `OpenSSL` 提供 TLS，证书 / 私钥从 PEM 文件加载
- 内置轻量 JSON 扁平解析器，仅支持一层 `key: value` 字符串映射
- 配置缓存加 `std::mutex`，按 `last_write_time` 判断是否需要重新加载
- 同时监听 HTTP 与 HTTPS；HTTPS 证书加载失败时自动降级为仅 HTTP

## 安全提示

本项目定位为本地 / 内网工具，默认监听 `0.0.0.0` 且无任何鉴权：

- 同一局域网内的机器只要能访问 5666/5667，就能查询别名列表并触发跳转
- 自签名证书仅用于消除浏览器地址栏的“不安全”提示，不具备公网可信性
- `cert.pfx` 使用硬编码密码 `alias123`，请勿用于生产环境
- 修改 hosts 与端口转发需要管理员权限，请确认来源可信后再运行脚本
