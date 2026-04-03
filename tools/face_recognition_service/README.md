# Face Recognition Service

这个目录里的服务运行在本地 PC 上，用来接收 ESP32 设备拍照上传的图片，并和本机 `photos/` 目录里的人脸库做比对。

当前实现基于：

- `opencv-contrib-python-headless`
- `LBPHFaceRecognizer`
- `FastAPI + Uvicorn`

这样做的目的，是避免 `dlib/face_recognition` 在 Windows 上依赖 Visual C++ 编译环境。

## 这套服务负责什么

- 本地保存已知人脸照片
- 在 PC 上构建本地识别模型
- 提供 `POST /recognize` 接口给设备上传图片
- 返回 `name / matched / confidence / message` 给设备端

## 目录结构

- `photos/`
  本地人脸库
- `data/lbph_model.yml`
  训练后的 OpenCV LBPH 模型
- `data/labels.json`
  标签到姓名的映射
- `app.py`
  HTTP 服务
- `rebuild_embeddings.py`
  重新构建本地模型
- `mcp_server.py`
  可选的外部 MCP 服务

## 人脸库照片怎么放

推荐两种方式，任选一种：

- `photos/Alice.jpg`
- `photos/Alice/1.jpg`
- `photos/Alice/2.jpg`

如果使用子目录，子目录名就是最终识别出来的人名。

建议每个人至少准备 `5~10` 张照片：

- 正脸
- 轻微左转/右转
- 不同距离
- 不同光照
- 每张图里尽量只有一个清晰人脸

## 一次性安装

```powershell
cd E:\OneDrive\Research\Project\Robot\RIG-puppy\tools\face_recognition_service
python -m venv .venv
. .\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
pip install -r requirements.txt
```

如果之前这个虚拟环境装过旧版依赖，建议先删掉重建：

```powershell
deactivate
Remove-Item .venv -Recurse -Force
python -m venv .venv
. .\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
pip install -r requirements.txt
```

## 启动前准备

1. 把参考照片放进 `photos/`
2. 激活虚拟环境
3. 重新构建本地识别模型

命令如下：

```powershell
cd E:\OneDrive\Research\Project\Robot\RIG-puppy\tools\face_recognition_service
. .\.venv\Scripts\Activate.ps1
python rebuild_embeddings.py
```

正常输出类似：

```text
Built LBPH model with 1 labels.
INFO Rebuilt LBPH model with 1 labels and 2 samples
```

## 如何启动服务

```powershell
cd E:\OneDrive\Research\Project\Robot\RIG-puppy\tools\face_recognition_service
. .\.venv\Scripts\Activate.ps1
uvicorn app:app --host 0.0.0.0 --port 8000
```

看到下面这几行，说明服务已经启动：

```text
INFO:     Started server process ...
INFO:     Waiting for application startup.
INFO:     Application startup complete.
INFO:     Uvicorn running on http://0.0.0.0:8000
```

这里的 `0.0.0.0` 表示监听所有网卡，不是实际访问地址。

## 如何确认服务能访问

先在本机浏览器打开：

- `http://127.0.0.1:8000/health`

正常应返回 JSON。

然后查电脑的局域网 IP：

```powershell
ipconfig
```

假设你的 IPv4 是 `172.20.10.2`，那设备实际访问的地址就是：

- `http://172.20.10.2:8000/recognize`

注意：

- 浏览器直接打开 `/recognize` 返回 `405 Method Not Allowed` 是正常的
- 因为 `/recognize` 只接受 `POST`
- 设备上传图片时才会真正调用它

## 设备联调顺序

1. 保持 `uvicorn` 窗口一直开着
2. 确认设备已经刷入带 `self.face_recognition.*` 的固件
3. 设备端人脸识别服务地址指向：
   `http://172.20.10.2:8000/recognize`
4. 对设备说：
   `看看目前是谁`

## 运行时如何判断有没有成功

当设备真的调用到服务时，`uvicorn` 窗口会看到类似：

```text
INFO Recognize request received, question=..., bytes=13481
INFO Recognize result: {'success': True, 'matched': False, 'name': 'Unknown', ...}
INFO:     172.20.10.14:55922 - "POST /recognize HTTP/1.1" 200 OK
```

这几项分别表示：

- `Recognize request received`
  设备已经拍照并上传成功
- `Recognize result`
  PC 侧识别结果
- `POST /recognize ... 200 OK`
  接口调用成功返回

## 常见结果怎么理解

- `No clear single face was detected in the current photo.`
  当前照片里没有检测到清晰单人脸，或脸太小、太偏、太糊

- `A face was detected, but it did not match any known person in the local database.`
  检测到了脸，但和本地照片库差异太大，没有匹配上

如果经常识别不到，优先改这几件事：

- 增加每个人的训练照片数量
- 让测试时人脸更靠近镜头
- 确保画面里只有一个人
- 光照更稳定

## 相关接口

- `GET /health`
  检查服务是否存活
- `GET /people`
  查看当前模型里有哪些名字
- `POST /rebuild`
  重新读取 `photos/` 并重建模型
- `POST /recognize`
  设备上传图片并进行识别

## 可选的外部 MCP 服务

这个目录里还带了一个可选的 `mcp_server.py`，它暴露的是数据库维护和本地调试工具，不负责直接从 ESP 相机取图。

可用工具：

- `list_known_people`
- `rebuild_face_database`
- `recognize_image_file`

启动方式：

```powershell
cd E:\OneDrive\Research\Project\Robot\RIG-puppy\tools\face_recognition_service
. .\.venv\Scripts\Activate.ps1
python mcp_server.py
```

## 当前方案的边界

这套服务是“设备端拍照 + PC 端识别”的混合方案。

纯外部 MCP 不能直接替代设备拍照，原因是：

- 外部 MCP 可以提供工具
- 但它拿不到板子摄像头的原始图像
- 所以仍需要设备端先抓拍，再把图片传到本机服务
