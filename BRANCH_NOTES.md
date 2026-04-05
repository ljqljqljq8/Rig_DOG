# Branch Notes

This branch builds on `codex/face-mcp-package` and packages the latest local
working state of the Rig Dog firmware plus the Windows face-service project.

It keeps the cleaned repo layout from the earlier package branch, then adds the
newer multi-person recognition behavior and several runtime-stability fixes that
were developed afterward.

## What this branch contains

- firmware source stays at repo root
- local face service stays under `mcp_service/`
- personal face photos and embeddings remain excluded from git
- one person can keep multiple face photos under `mcp_service/photos/<name>/`
- the dog can recognize faces, enroll by taking a photo, and locate a named person
- the face service can return multiple face matches from one image
- locate logic now selects the requested target person instead of following the most obvious face
- firmware side includes tool-call and audio-abort stability fixes used in the current local setup

See `docs/face_mcp_status.md` for the detailed feature status.

## How this branch differs from `codex/face-mcp-package`

`codex/face-mcp-package` established the cleaned repo layout and the first
working face-MCP package. This branch keeps that structure but updates behavior.

Key differences in this branch:

- `/recognize` can now report multiple faces in one frame instead of collapsing to a single result
- `/locate` now scores every detected face against the requested identity before choosing a target
- service-side tests were added for multi-face recognition and target selection
- firmware-side MCP tool execution is guarded so overlapping tool calls fail fast instead of stacking
- speaking abort now resets the decoder path to reduce stuck state after interruptions or failures
- the Lulu board config is updated to the current service host used in the latest local setup

## How this branch differs from `main`

`main` is still close to the earlier import state of the repo:

- it includes a large `build_lulu/` build output tree in git
- it tracks `sdkconfig`
- it does not contain the current cleaned local face-service layout
- it does not document the current working face MCP setup

This branch changes that by:

- removing tracked firmware build artifacts from git
- removing tracked local machine config from git
- adding `mcp_service/` as the local face-service project
- adding branch-specific docs for the current face MCP workflow
- keeping the repo aligned with the code that is actually working locally

## How this branch differs from `face_v1`

`face_v1` is an earlier face-recognition prototype branch. It added a service,
but its structure and behavior are older than the current setup.

Key differences in this branch:

- the local service lives in `mcp_service/` instead of `tools/face_recognition_service/`
- no sample face photos are committed
- no `embeddings.json` is committed
- the dog can enroll a person directly by taking a photo, not just recognize
- the local face library supports multiple images per person
- the local service exposes `/locate` for target-position feedback
- the local service can identify multiple faces in one image
- locate now resolves a requested target within multi-person scenes
- the firmware side includes dedicated tools such as `self.camera.face_rec`
- the firmware side includes dedicated tools such as `self.camera.face_enroll`
- the firmware side includes dedicated tools such as `self.camera.remember_person`
- the firmware side includes `self.camera.locate_person`
- the firmware side includes `self.dog.follow_person`

## When to use this branch

Use this branch when you want:

- the cleaned repo structure
- the current local face MCP implementation
- privacy-safe handling of local face data
- multi-person recognition and improved target selection
- the code layout that matches the machine and service setup currently being used
