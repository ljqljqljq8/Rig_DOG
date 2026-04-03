# Branch Notes

This branch is the current integration branch for the working Rig Dog face MCP
setup. It is not just a small feature patch. It repackages the repo into the
structure that matches the code currently running locally.

## What this branch contains

- firmware source stays at repo root
- local face service is moved into `mcp_service/`
- personal face photos and embeddings are excluded from git
- dog-side face recognition and face enrollment hooks are already wired in

See `docs/face_mcp_status.md` for the detailed feature status.

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

- the local service now lives in `mcp_service/` instead of `tools/face_recognition_service/`
- no sample face photos are committed
- no `embeddings.json` is committed
- the dog can enroll a person directly by taking a photo, not just recognize
- the firmware side includes dedicated tools such as `self.camera.face_rec`
- the firmware side includes dedicated tools such as `self.camera.face_enroll`
- the firmware side includes dedicated tools such as `self.camera.remember_person`
- the generic camera flow can redirect "remember/register" phrasing into face enrollment
- the branch also cleans up old tracked build outputs instead of only adding feature files

## When to use this branch

Use this branch when you want:

- the cleaned repo structure
- the current local face MCP implementation
- privacy-safe handling of local face data
- the code layout that matches the machine and service setup currently being used
