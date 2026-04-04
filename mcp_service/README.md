# Rig Dog Face Recognition Service

This project provides a local face recognition HTTP service for `RIG-puppy`.

It keeps the same workflow as the Seeed tutorial:

- source photos live in `photos/`
- embeddings are stored in `data/embeddings.json`
- the dog calls `POST /recognize`

The implementation here is Windows-friendly and uses `insightface` on CPU instead of the Hailo-only stack from the original tutorial.

## Layout

```text
Rig_DOG/
  data/
    embeddings.json
  photos/
    alice/
      20260404_120001_123456.jpg
      20260404_120530_654321.jpg
    bob/
      front.png
      side.jpg
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

## Requirements

- Windows
- Python 3.12 recommended
- Dog and PC on the same LAN

## Current LAN URL

The firmware patch in this workspace is configured to call:

```text
http://192.168.147.46:8001/recognize
```

If your PC IP changes, update `FACE_RECOGNITION_URL`, `FACE_ENROLL_URL`, and `FACE_TRACK_URL` in:

- `D:\Roboagent\Robot\Robot\RIG-puppy\main\boards\lulu-esp32s3\config.h`

Then rebuild and reflash the firmware.

## Prepare the face library

Put each person's photos into their own folder under `photos/`.

Examples:

```text
photos/
  alice/
    front.jpg
    side.jpg
    smiling.png
  bob/
    office.jpg
  charlie/
    1.jpeg
    2.jpeg
```

The folder name becomes the enrolled person name.

Legacy flat files like `photos/alice.jpg` are automatically migrated into `photos/alice/alice.jpg` when the service starts or when batch processing runs.

## First-time setup

```powershell
cd D:\Roboagent\MCP\Rig_DOG
C:\Python312\python.exe -m venv .venv
.\.venv\Scripts\python.exe -m pip install --upgrade pip
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
```

Or use:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\start_windows.ps1 -SetupOnly
```

## Build the embeddings file

```powershell
cd D:\Roboagent\MCP\Rig_DOG
.\.venv\Scripts\python.exe .\scripts\batch_process.py
```

This scans all person folders under `photos/` and writes `data/embeddings.json`.

## Start the service

```powershell
cd D:\Roboagent\MCP\Rig_DOG
.\.venv\Scripts\python.exe -m uvicorn src.app:app --host 0.0.0.0 --port 8001
```

Or use:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\start_windows.ps1
```

Open:

- `http://127.0.0.1:8001/health`
- `http://127.0.0.1:8001/list`

## Main endpoints

- `GET /health`
- `GET /list`
- `POST /recognize`
- `POST /locate`
- `POST /enroll`
- `POST /reload`
- `DELETE /remove/{name}`
- `POST /detect_and_embed`

## Notes

- The first startup downloads the `insightface` model into `models_cache/`.
- `POST /enroll` now appends a new photo into that person's folder instead of overwriting a single image.
- Recognition now supports multiple embeddings per person and uses the best match across that person's photos.
- `POST /locate` is intended for follow behavior and returns whether a target person matches plus face position data such as horizontal offset and face size ratio.
- The dog firmware in this workspace did not originally include a face recognition MCP tool. A firmware-side patch is required and was added separately in `RIG-puppy`.
- If the dog is already running the patched firmware that calls this local service, face-library changes only require restarting the local service or running `/reload`. Reflash is only needed when the firmware-side code or recognition URL changes.
