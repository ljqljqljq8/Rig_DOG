# Rig Dog Face MCP Status

This branch packages the current working state of the project into one repo:

- firmware source at repo root
- local face service in `mcp_service/`
- no local face photos or embeddings committed

## What works in the current local setup

- The dog can capture a photo and recognize a face against the local PC library.
- The dog can capture a photo and enroll a person directly into the local PC library.
- The dog can reuse natural phrases such as "记住我叫某某" and route them into face enrollment.
- The local HTTP service exposes `/recognize`, `/enroll`, `/reload`, `/list`, and `/health`.

## Dog-side MCP tools currently added

- `self.camera.face_rec`
- `self.camera.face_enroll`
- `self.camera.remember_person`

`self.camera.take_photo` also has a redirect path for "remember/register" phrasing
so face enrollment can still succeed when the assistant selects the generic camera tool.

## Privacy rule in this branch

This branch does not upload any personal face library files:

- no photos from `mcp_service/photos/`
- no embeddings from `mcp_service/data/embeddings.json`

After cloning, each collaborator should enroll faces locally on their own machine.

## Current board-side assumptions

- board: Lulu ESP32-S3
- local face service URL: `http://192.168.147.46:8001`
- endpoints used by firmware:
  - `/recognize`
  - `/enroll`

If the service host IP changes, update `main/boards/lulu-esp32s3/config.h`,
then rebuild and reflash the firmware.
