# Rig Dog 本地人脸服务与固件联调说明

本文档面向当前仓库里的 `Lulu ESP32-S3 + 本地 PC 人脸服务` 组合，覆盖以下内容：

- 固件编译、烧录、串口监视
- 本地人脸服务安装、启动、验证
- 板端与 PC 端配置对齐
- 人脸库目录规则与自动追加逻辑
- 常用 API 与常见问题排查

当前仓库结构里：

- 仓库根目录是 ESP-IDF 固件工程
- [`mcp_service/`](/e:/OneDrive/Research/Project/Robot_Pet_Project/Rig_Puppy/Rig_DOG/mcp_service) 是本地人脸服务
- 本地照片与 embedding 不会提交到 git

## 1. 适用环境

当前这套说明默认基于以下环境：

- Windows
- ESP-IDF 已安装在 `C:\Espressif\esp-idf`
- Python 推荐 `3.12`，兼容 `3.11`
- 串口为 `COM9`
- PC 当前局域网 IP 为 `172.20.10.2`
- 板子型号为 `Lulu ESP32-S3`

如果你的环境不同，主要改这几类参数：

- 串口：`COM9`
- PC IP：`172.20.10.2`
- Python 安装路径

## 2. 目录说明

```text
Rig_DOG/
  main/
    boards/lulu-esp32s3/config.h     # 板端 face 服务地址
  sdkconfig.lulu-esp32s3.local       # 本地 Lulu 构建配置
  build_lulu_mcp/                    # 推荐构建目录
  mcp_service/
    data/
      embeddings.json                # 本地 embedding，忽略 git
    photos/
      <name>/
        1.jpg
        2.jpg
    scripts/
      start_windows.ps1              # Windows 启动脚本
      batch_process.py               # 批量重建 embedding
    src/
      app.py
      config.py
      photo_processor.py
      vector_store.py
```

## 3. 固件环境激活

在仓库根目录执行：

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
. ..\activate_idf.ps1
$env:PATH = 'C:\Program Files\Git\cmd;' + $env:PATH
```

说明：

- [`activate_idf.ps1`](/e:/OneDrive/Research/Project/Robot_Pet_Project/Rig_Puppy/activate_idf.ps1) 会激活 ESP-IDF，并清理掉会干扰 ESP-IDF 的 MinGW/MSYS 路径
- 激活后额外把 `C:\Program Files\Git\cmd` 加回 `PATH`，避免某些 CMake/Git 探测报错

## 4. 固件编译、烧录、监视

### 4.1 推荐构建参数

当前推荐固定使用：

- 构建目录：`build_lulu_mcp`
- 本地配置文件：`sdkconfig.lulu-esp32s3.local`
- 目标芯片：`esp32s3`

原因是这个分支直接用默认 `sdkconfig` 容易落到错误 target，Lulu 这套配置建议显式指定。

### 4.2 只编译

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
. ..\activate_idf.ps1
$env:PATH = 'C:\Program Files\Git\cmd;' + $env:PATH
python $env:IDF_PATH\tools\idf.py -B build_lulu_mcp -D SDKCONFIG=sdkconfig.lulu-esp32s3.local -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.lulu-esp32s3" -D IDF_TARGET=esp32s3 build
```

### 4.3 编译并烧录

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
. ..\activate_idf.ps1
$env:PATH = 'C:\Program Files\Git\cmd;' + $env:PATH
python $env:IDF_PATH\tools\idf.py -B build_lulu_mcp -D SDKCONFIG=sdkconfig.lulu-esp32s3.local -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.lulu-esp32s3" -D IDF_TARGET=esp32s3 -p COM9 build flash
```

### 4.4 只烧录

如果已经编译过，只想重新刷写：

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
. ..\activate_idf.ps1
$env:PATH = 'C:\Program Files\Git\cmd;' + $env:PATH
python $env:IDF_PATH\tools\idf.py -B build_lulu_mcp -D SDKCONFIG=sdkconfig.lulu-esp32s3.local -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.lulu-esp32s3" -D IDF_TARGET=esp32s3 -p COM9 flash
```

### 4.5 串口监视

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
. ..\activate_idf.ps1
$env:PATH = 'C:\Program Files\Git\cmd;' + $env:PATH
python $env:IDF_PATH\tools\idf.py -B build_lulu_mcp -D SDKCONFIG=sdkconfig.lulu-esp32s3.local -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.lulu-esp32s3" -D IDF_TARGET=esp32s3 -p COM9 monitor
```

### 4.6 烧录后立刻进入监视

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
. ..\activate_idf.ps1
$env:PATH = 'C:\Program Files\Git\cmd;' + $env:PATH
python $env:IDF_PATH\tools\idf.py -B build_lulu_mcp -D SDKCONFIG=sdkconfig.lulu-esp32s3.local -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.lulu-esp32s3" -D IDF_TARGET=esp32s3 -p COM9 flash monitor
```

### 4.7 常见固件侧问题

- `COM9` 被占用：
  先关闭串口监视器、串口助手、上一次 `idf.py monitor` 窗口，再重刷
- 改了 `mcp_service` 代码：
  不需要重刷，只需要重启 PC 端服务
- 改了 [`config.h`](/e:/OneDrive/Research/Project/Robot_Pet_Project/Rig_Puppy/Rig_DOG/main/boards/lulu-esp32s3/config.h) 里的 IP/端口：
  需要重新 `build flash`

## 5. 本地人脸服务安装与启动

### 5.1 一键安装并启动

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG\mcp_service
powershell -ExecutionPolicy Bypass -File .\scripts\start_windows.ps1
```

[`start_windows.ps1`](/e:/OneDrive/Research/Project/Robot_Pet_Project/Rig_Puppy/Rig_DOG/mcp_service/scripts/start_windows.ps1) 会自动：

- 优先寻找 Python 3.12，其次 3.11
- 创建 `.venv`
- 安装 `requirements.txt`
- 启动 `uvicorn`

### 5.2 只安装，不启动

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG\mcp_service
powershell -ExecutionPolicy Bypass -File .\scripts\start_windows.ps1 -SetupOnly
```

### 5.3 手动安装

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG\mcp_service
C:\Users\Lenovo\AppData\Local\Programs\Python\Python312\python.exe -m venv .venv
.\.venv\Scripts\python.exe -m pip install --upgrade pip
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
```

### 5.4 手动启动

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG\mcp_service
.\.venv\Scripts\python.exe -m uvicorn src.app:app --host 0.0.0.0 --port 8001
```

## 6. 服务启动后的验证命令

### 6.1 健康检查

```powershell
Invoke-RestMethod http://127.0.0.1:8001/health
Invoke-RestMethod http://172.20.10.2:8001/health
```

正常情况下会返回类似：

```json
{
  "status": "ok",
  "loaded_users": 1,
  "embeddings_file": "...\\mcp_service\\data\\embeddings.json",
  "last_reload": "...",
  "uptime_ms": 12345
}
```

### 6.2 查看当前人脸库

```powershell
Invoke-RestMethod http://127.0.0.1:8001/list
```

### 6.3 从 `photos/` 重新构建 embedding

```powershell
Invoke-RestMethod -Method Post -Uri http://127.0.0.1:8001/reload -ContentType 'application/json' -Body '{}'
```

### 6.4 删除一个人

```powershell
Invoke-RestMethod -Method Delete -Uri http://127.0.0.1:8001/remove/大酷盖
```

## 7. 板端与服务端配置对齐

当前板端 face 服务地址在：

- [config.h](/e:/OneDrive/Research/Project/Robot_Pet_Project/Rig_Puppy/Rig_DOG/main/boards/lulu-esp32s3/config.h)

当前默认值：

- `FACE_RECOGNITION_URL = http://172.20.10.2:8001/recognize`
- `FACE_ENROLL_URL = http://172.20.10.2:8001/enroll`

如果你的 PC IP 改了：

1. 修改 [`config.h`](/e:/OneDrive/Research/Project/Robot_Pet_Project/Rig_Puppy/Rig_DOG/main/boards/lulu-esp32s3/config.h)
2. 重新执行 `build flash`
3. 重新启动本地人脸服务

另外需要保证：

- 板子和 PC 在同一局域网
- Windows 防火墙允许 `8001`
- 服务监听地址是 `0.0.0.0`

## 8. 人脸库目录规则

推荐目录结构：

```text
mcp_service/
  photos/
    大酷盖/
      1.jpg
      2.jpg
      3.jpg
    Alice/
      1.jpg
      2.jpg
```

规则如下：

- 一个人一个目录
- 后续新样本继续追加到该目录
- `reload` 会把同一目录下的多张照片聚合成一个人的 embedding
- 旧格式 `photos/Alice.jpg` 仍兼容，但不再推荐

## 9. 识别、录入与自动追加逻辑

### 9.1 `/enroll`

当板子执行“记住我”“把我记成 Alice”这类动作时，会调用 `/enroll`：

- 会把当前照片保存到 `photos/<name>/`
- 会重建该人的 embedding
- 如果同名目录已存在，会继续追加新样本，不会覆盖旧图

### 9.2 `/recognize`

当板子执行“你认识我吗”时，会调用 `/recognize`：

- 先做人脸识别
- 如果命中已知人，且置信度足够高，会自动把这张照片追加进该人的目录
- 追加后会重建该人的 embedding

注意：

- 板端识别门槛当前是 `0.45`
- 服务端自动追加照片门槛默认是 `0.60`

所以可能出现：

- 板子已经说“我认识你”
- 但这张照片没有被自动追加

这通常是因为识别置信度在 `0.45 ~ 0.60` 之间。

## 10. 中文同音名/拼音近似归并

当前服务支持按拼音或近似读音把名字归并到已有目录。例如：

- 已有目录：`photos/大酷盖/`
- 后续语音识别得到：`大裤盖`

服务会优先尝试归并到已有目录 `大酷盖`，而不是新建一个 `大裤盖/` 重复目录。

这个逻辑主要用于减少：

- ASR 听写错字
- 同音不同字
- 近似拼音导致的重复人名

注意：

- 如果两个不同的人本来就同音，这种自动归并可能会合并错人
- 这种情况下可以关闭或调高相关阈值

## 11. 可调环境变量

这些变量在服务启动前设置：

```powershell
$env:HOST = "0.0.0.0"
$env:PORT = "8001"
$env:SIMILARITY_THRESHOLD = "0.45"
$env:AUTO_APPEND_RECOGNIZED_PHOTOS = "1"
$env:AUTO_APPEND_MIN_CONFIDENCE = "0.60"
$env:PHONETIC_ALIAS_MATCH_ENABLED = "1"
$env:PHONETIC_ALIAS_MIN_SIMILARITY = "0.92"
powershell -ExecutionPolicy Bypass -File .\scripts\start_windows.ps1
```

说明：

- `SIMILARITY_THRESHOLD`
  识别是否命中的阈值，默认 `0.45`
- `AUTO_APPEND_RECOGNIZED_PHOTOS`
  识别成功后是否自动追加样本，`1` 为开启
- `AUTO_APPEND_MIN_CONFIDENCE`
  自动追加样本的最低置信度，默认 `0.60`
- `PHONETIC_ALIAS_MATCH_ENABLED`
  是否启用同音/拼音近似归并
- `PHONETIC_ALIAS_MIN_SIMILARITY`
  拼音近似归并阈值，越高越保守

如果你希望“认出来就尽量存”，可以把自动追加门槛降到和识别门槛一致：

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG\mcp_service
$env:AUTO_APPEND_MIN_CONFIDENCE = "0.45"
powershell -ExecutionPolicy Bypass -File .\scripts\start_windows.ps1
```

## 12. 常用 face API

### 12.1 健康检查

```powershell
Invoke-RestMethod http://127.0.0.1:8001/health
```

### 12.2 列出当前人员

```powershell
Invoke-RestMethod http://127.0.0.1:8001/list
```

### 12.3 从本地照片重建库

```powershell
Invoke-RestMethod -Method Post -Uri http://127.0.0.1:8001/reload -ContentType 'application/json' -Body '{}'
```

### 12.4 删除名字拼错的旧条目

```powershell
Invoke-RestMethod -Method Delete -Uri http://127.0.0.1:8001/remove/大裤盖
Invoke-RestMethod -Method Post -Uri http://127.0.0.1:8001/reload -ContentType 'application/json' -Body '{}'
```

## 13. 典型使用流程

### 13.1 首次搭建

1. 启动本地人脸服务
2. 确认 `/health` 正常
3. 固件 `build flash`
4. 串口监视确认板子连上 Wi-Fi
5. 对板子说“记住我，我叫大酷盖”
6. 再说“你认识我吗”

### 13.2 只改了 `mcp_service`

1. 保存 Python 代码
2. 重启本地服务
3. 不需要重刷固件

### 13.3 只改了板端地址或固件逻辑

1. 修改 C++/`config.h`
2. 重新 `build flash`
3. 重启串口监视

## 14. 常见问题

### 14.1 `loaded_users` 比 `photos/` 目录里的人数多

说明 `embeddings.json` 里还残留旧名字。执行：

```powershell
Invoke-RestMethod -Method Post -Uri http://127.0.0.1:8001/reload -ContentType 'application/json' -Body '{}'
```

### 14.2 板子能识别，但没有新增照片

先确认两件事：

- 你已经重启了本地服务，确保跑的是最新代码
- 识别置信度是否低于 `AUTO_APPEND_MIN_CONFIDENCE`

重启后观察服务日志：

- `appended recognized sample ...` 表示已经追加成功
- `skipped auto-append ... below threshold ...` 表示因置信度不足被跳过

### 14.3 `COM9` 被占用，无法烧录

关闭这些可能占串口的进程：

- `idf.py monitor`
- 串口助手
- IDE 自带串口监视器

### 14.4 `http://172.20.10.2:8001/recognize` 连不上

检查：

- 服务是否已启动
- `/health` 是否正常
- Windows 防火墙是否放行 `8001`
- 板子和 PC 是否在同一网段

### 14.5 Python 安装失败

不要用 Python 3.14 建这个环境。当前推荐：

- Python 3.12
- 或 Python 3.11

脚本会优先选择这两个版本。

## 15. 隐私说明

本仓库不会把本地人脸数据提交到 git：

- `mcp_service/photos/` 内容本地保留
- `mcp_service/data/embeddings.json` 本地保留

因此每个协作者需要在自己的电脑上单独建立人脸库。
