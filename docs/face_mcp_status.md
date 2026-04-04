# Rig Dog Face MCP Status

This branch packages the current working state of the project into one repo:

- firmware source at repo root
- local face service in `mcp_service/`
- no local face photos or embeddings committed

## What works in the current local setup

- The dog can capture a photo and recognize a face against the local PC library.
- The dog can capture a photo and enroll a person directly into the local PC library.
- The dog can reuse natural phrases such as "remember me as <name>" and route them into face enrollment.
- The local HTTP service exposes `/recognize`, `/enroll`, `/reload`, `/list`, and `/health`.

## Branch comparison

### Compared with `main`

This branch is different from `main` in two major ways:

- it is a cleaned source branch, not a branch that still tracks large `build_lulu/` outputs
- it includes the current working face MCP integration instead of only the old imported firmware tree

Practically, that means:

- `build_lulu/` is removed from git
- `sdkconfig` is removed from git
- `mcp_service/` is added as the local face-service project
- current face-MCP documentation is added

### Compared with `face_v1`

`face_v1` is an earlier prototype branch for face recognition. This branch goes further.

`face_v1` mainly added:

- an older service under `tools/face_recognition_service/`
- basic firmware integration
- an example face photo that was later removed

This branch instead provides:

- the current service under `mcp_service/`
- direct dog-side face enrollment support
- dedicated MCP tools for recognition and enrollment
- a privacy-safe repo policy where photos and embeddings stay local
- a cleaned repo tree that removes tracked build artifacts

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

## Repository behavior for face data

Local face data is intentionally kept out of git:

- `mcp_service/photos/*.jpg` stays local
- `mcp_service/data/embeddings.json` stays local

This means collaborators can share the code branch without sharing personal biometric data.

## Current board-side assumptions

- board: Lulu ESP32-S3
- local face service URL: `http://172.20.10.2:8001`
- firmware endpoint: `/recognize`
- firmware endpoint: `/enroll`

If the service host IP changes, update `main/boards/lulu-esp32s3/config.h`,
then rebuild and reflash the firmware.
