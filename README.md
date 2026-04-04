# Rig_DOG `face_v2`

这个分支用于 `Lulu ESP32-S3` 机器狗的人脸 MCP 本地联调，当前已经合并了两类能力：

- 本地 face service：
  每人多图、同音或近似拼音归并、识别后自动追加样本。
- 固件侧 follow 能力：
  `self.camera.locate_person` 和 `self.dog.follow_person`。

当前默认联调环境按这台开发机配置：

- 仓库路径：`E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG`
- 本机服务地址：`172.20.10.2:8001`
- 串口：`COM9`
- 板卡：`Lulu ESP32-S3`

## 目录说明

```text
Rig_DOG/
  main/                       固件源码
  mcp_service/                Windows 本地 face MCP 服务
  build_lulu_mcp/             旧的人脸 MCP 构建产物
  build_lulu_follow/          当前 follow 版构建产物
  sdkconfig.lulu-esp32s3.local
```

## 当前能力

- `self.camera.face_rec`
- `self.camera.face_enroll`
- `self.camera.remember_person`
- `self.camera.locate_person`
- `self.dog.follow_person`

本地服务支持：

- `POST /recognize`
- `POST /enroll`
- `POST /reload`
- `POST /locate`
- `DELETE /remove/{name}`
- `GET /list`
- `GET /health`

## 启动本地服务

推荐直接使用脚本：

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG\mcp_service
powershell -ExecutionPolicy Bypass -File .\scripts\start_windows.ps1
```

手动启动：

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG\mcp_service
.\.venv\Scripts\Activate.ps1
python -m uvicorn src.app:app --host 0.0.0.0 --port 8001
```

健康检查：

```powershell
Invoke-WebRequest http://127.0.0.1:8001/health
Invoke-WebRequest http://172.20.10.2:8001/health
```

## ESP-IDF 环境激活

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
. ..\activate_idf.ps1
$env:PATH = 'C:\Program Files\Git\cmd;' + $env:PATH
```

## 构建固件

当前推荐使用 `build_lulu_follow`：

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
. ..\activate_idf.ps1
$env:PATH = 'C:\Program Files\Git\cmd;' + $env:PATH
python $env:IDF_PATH\tools\idf.py -B build_lulu_follow -D SDKCONFIG=sdkconfig.lulu-esp32s3.local -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.lulu-esp32s3" -D IDF_TARGET=esp32s3 build
```

如果要复用旧目录 `build_lulu_mcp`：

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
. ..\activate_idf.ps1
$env:PATH = 'C:\Program Files\Git\cmd;' + $env:PATH
python $env:IDF_PATH\tools\idf.py -B build_lulu_mcp -D SDKCONFIG=sdkconfig.lulu-esp32s3.local -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.lulu-esp32s3" -D IDF_TARGET=esp32s3 build
```

## 烧录与监视

烧录：

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
. ..\activate_idf.ps1
$env:PATH = 'C:\Program Files\Git\cmd;' + $env:PATH
python $env:IDF_PATH\tools\idf.py -B build_lulu_follow -D SDKCONFIG=sdkconfig.lulu-esp32s3.local -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.lulu-esp32s3" -D IDF_TARGET=esp32s3 -p COM9 flash
```

串口监视：

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
. ..\activate_idf.ps1
$env:PATH = 'C:\Program Files\Git\cmd;' + $env:PATH
python $env:IDF_PATH\tools\idf.py -B build_lulu_follow -D SDKCONFIG=sdkconfig.lulu-esp32s3.local -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.lulu-esp32s3" -D IDF_TARGET=esp32s3 -p COM9 monitor
```

一条命令直接烧录并监视：

```powershell
cd E:\OneDrive\Research\Project\Robot_Pet_Project\Rig_Puppy\Rig_DOG
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
. ..\activate_idf.ps1
$env:PATH = 'C:\Program Files\Git\cmd;' + $env:PATH
python $env:IDF_PATH\tools\idf.py -B build_lulu_follow -D SDKCONFIG=sdkconfig.lulu-esp32s3.local -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.lulu-esp32s3" -D IDF_TARGET=esp32s3 -p COM9 flash monitor
```

## 人脸库说明

人脸库位于 `mcp_service/photos/`，按“每人一个文件夹”维护：

```text
mcp_service/photos/
  大酷盖/
    1.jpg
    2.jpg
    3.jpg
```

当前行为：

- 同一个人再次录入时，会继续往对应目录追加图片。
- 识别命中已知人时，会按阈值自动追加新图片。
- 同音或相近拼音名会优先归并到已有目录名。

## 已知限制

- 当前 `lulu.bin` 仍然大于 `ota_1` 分区。
- 串口 `flash` 正常，但 OTA 分区布局后续需要单独调整。
- 这个分支把 `build_lulu_mcp/` 和 `build_lulu_follow/` 也一并保留在仓库中，仓库体积会比较大。

## 更多说明

本地服务的详细说明见 [mcp_service/README.md](mcp_service/README.md)。
