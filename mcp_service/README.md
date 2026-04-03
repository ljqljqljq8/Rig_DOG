# Rig Dog Local Face Service

This directory contains the local MCP-side face service used by the dog.

It provides a Windows-friendly HTTP API based on `insightface` + `onnxruntime`
and is meant to run on the same LAN as the dog.

## Repo layout

```text
mcp_service/
  data/
    .gitkeep
    embeddings.json        # local only, ignored by git
  photos/
    .gitkeep
    <name>.jpg             # local only, ignored by git
  scripts/
    batch_process.py
    start_windows.ps1
  src/
    app.py
    config.py
    face_backend.py
    photo_processor.py
    vector_store.py
```

## Privacy and git behavior

This repo intentionally does not upload face data:

- `photos/*.jpg` is ignored by git
- `data/embeddings.json` is ignored by git

Each collaborator needs to build their own local face library after cloning.

## Current capabilities

- `POST /recognize`: compare a camera frame against the local face library
- `POST /enroll`: save a captured face under a given name
- `POST /reload`: rebuild embeddings from the local `photos/` folder
- `DELETE /remove/{name}`: delete a person from the local face library
- `GET /list`: show current locally enrolled names
- `GET /health`: service health check

## Local setup

```powershell
cd <repo>\mcp_service
C:\Python312\python.exe -m venv .venv
.\.venv\Scripts\python.exe -m pip install --upgrade pip
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
```

Or:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\start_windows.ps1 -SetupOnly
```

## Start the service

```powershell
cd <repo>\mcp_service
powershell -ExecutionPolicy Bypass -File .\scripts\start_windows.ps1
```

Then open:

- `http://127.0.0.1:8001/health`
- `http://127.0.0.1:8001/list`

## Build the local face library

Option A: put one face photo per person into `photos/`, then run:

```powershell
cd <repo>\mcp_service
.\.venv\Scripts\python.exe .\scripts\batch_process.py
```

Option B: use the dog to enroll a face directly through the MCP toolchain.

When the same name is enrolled again, the local photo and embedding are
overwritten with the latest capture.

## Firmware integration

The firmware side in this repo already includes the dog-side hooks for this
service. Relevant files live in the firmware tree at repo root:

- `main/boards/common/camera.h`
- `main/boards/common/esp32_camera.h`
- `main/boards/common/esp32_camera.cc`
- `main/boards/lulu-esp32s3/config.h`
- `main/boards/lulu-esp32s3/lulu-esp32s3.cc`
- `main/mcp_server.cc`

The current board config points the dog to:

- `http://192.168.147.46:8001/recognize`
- `http://192.168.147.46:8001/enroll`

If the PC LAN IP changes, update the URLs in `main/boards/lulu-esp32s3/config.h`,
then rebuild and reflash the firmware.
