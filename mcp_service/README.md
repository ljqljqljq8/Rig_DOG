# Rig_DOG 本地 Face MCP 服务

这个目录是运行在 Windows 电脑上的本地 HTTP 服务，用来给 `Rig_DOG`
提供以下能力：

- 人脸识别
- 人脸录入
- 每人多图样本管理
- 同音或近似拼音人名归并
- 命中已知人后自动追加新照片
- `locate_person` 用的人脸位置估计

默认服务地址：

- `http://172.20.10.2:8001`

## 目录结构

```text
mcp_service/
  data/
    embeddings.json
  models_cache/
  photos/
    .gitkeep
    <name>/
      1.jpg
      2.jpg
  scripts/
    batch_process.py
    start_windows.ps1
  src/
    app.py
    config.py
    face_backend.py
    name_utils.py
    photo_processor.py
    vector_store.py
```

## 启动方式

### 方式一：直接用脚本

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG\mcp_service
powershell -ExecutionPolicy Bypass -File .\scripts\start_windows.ps1
```

### 方式二：手动激活并启动

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG\mcp_service
.\.venv\Scripts\Activate.ps1
python -m uvicorn src.app:app --host 0.0.0.0 --port 8001
```

## 安装依赖

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG\mcp_service
.\.venv\Scripts\python.exe -m pip install --upgrade pip
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
```

如果还没有 `.venv`：

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG\mcp_service
C:\Users\Lenovo\AppData\Local\Programs\Python\Python312\python.exe -m venv .venv
.\.venv\Scripts\python.exe -m pip install --upgrade pip
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
```

## 健康检查

```powershell
Invoke-WebRequest http://127.0.0.1:8001/health
Invoke-WebRequest http://172.20.10.2:8001/health
Invoke-WebRequest http://127.0.0.1:8001/list
```

## 当前接口

- `POST /recognize`
- `POST /enroll`
- `POST /reload`
- `POST /locate`
- `DELETE /remove/{name}`
- `GET /list`
- `GET /health`

## 人脸库规则

### 每人多图

照片按“每人一个文件夹”保存：

```text
photos/
  Alice/
    1.jpg
    2.jpg
  大酷盖/
    1.jpg
    2.jpg
    3.jpg
```

### 自动迁移旧格式

如果历史上还是这种旧格式：

```text
photos/
  Alice.jpg
```

服务启动后或重建时，会迁成：

```text
photos/
  Alice/
    Alice.jpg
```

### 自动追加

- 显式 `enroll` 同一个人时，会继续往对应目录追加照片。
- `recognize` 命中已知人且置信度达到阈值时，也会自动追加照片。

## 同音或近似拼音归并

服务会优先把同音或接近拼音的人名归并到已有目录名。例如：

- “大酷盖”
- “大裤盖”

如果库里已经存在其中一个，后续会优先归并到那个已有名字，而不是再新建一个近似重名目录。

相关配置在 `src/config.py`：

- `PHONETIC_ALIAS_MATCH_ENABLED`
- `PHONETIC_ALIAS_MIN_SIMILARITY`

## `/locate` 用途

`/locate` 是给固件侧 `self.camera.locate_person` 和 `self.dog.follow_person`
用的。它除了识别目标人，还会返回：

- `bbox`
- `center`
- `offset`
- `face_ratio`
- `turn_hint`
- `distance_hint`

机器狗固件会根据这些结果做一个短时闭环跟随。

## 重新扫描照片库

如果你手工往 `photos/` 里放了新照片，执行：

```powershell
Invoke-RestMethod -Method Post -Uri http://127.0.0.1:8001/reload -ContentType 'application/json' -Body '{}'
```

然后检查：

```powershell
Invoke-RestMethod http://127.0.0.1:8001/list
Invoke-RestMethod http://127.0.0.1:8001/health
```

## 清理某个人

```powershell
Invoke-RestMethod -Method Delete -Uri http://127.0.0.1:8001/remove/Alice
```

## 常用环境变量

```powershell
$env:SIMILARITY_THRESHOLD = "0.45"
$env:AUTO_APPEND_MIN_CONFIDENCE = "0.60"
$env:PHONETIC_ALIAS_MATCH_ENABLED = "1"
$env:PHONETIC_ALIAS_MIN_SIMILARITY = "0.92"
```

如果想让“识别命中”更容易自动追加照片，可以临时降低：

```powershell
$env:AUTO_APPEND_MIN_CONFIDENCE = "0.45"
python -m uvicorn src.app:app --host 0.0.0.0 --port 8001
```

## 常见问题

### 1. `WinError 10048`

说明 `8001` 端口已经被另一个旧服务实例占用了。先停掉旧进程，再重新启动当前 `.venv` 里的服务。

### 2. `TCP connection failed`

如果板端连不上本机服务，优先检查：

- 本地服务是否真的监听在 `0.0.0.0:8001`
- `http://172.20.10.2:8001/health` 是否可访问
- Windows 防火墙是否放行 `8001`

### 3. 识别成功但没有追加新照片

优先检查两件事：

- 当前运行的是不是最新服务实例
- 当前识别置信度是否低于 `AUTO_APPEND_MIN_CONFIDENCE`

## Git 行为

这个目录通常不应该把真人照片和本地 embedding 上传到仓库。当前仓库仍然保留：

- `photos/.gitkeep`
- 代码
- 文档

本地真人照片和 `embeddings.json` 是否进入仓库，请在提交前自行确认。
