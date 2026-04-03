# Face Recognition On Lulu ESP32-S3

## Recommended Architecture

For the current Lulu hardware, the practical architecture is:

1. The ESP32 device captures the current image with the onboard camera
2. The device uploads that image to a face recognition service running on the local PC
3. The PC service compares the face against a local folder-based face database
4. The service returns compact JSON with `name`, `matched`, and `confidence`
5. The model speaks the result

This keeps the face database and embedding workload on the PC instead of the ESP32.

## Why Not Pure External MCP Only

The external MCP endpoint can expose tools to the model, but it cannot directly access the ESP32 camera image unless the device provides that image through another path.

In this project, the existing image upload path is already implemented in:

- `self.camera.take_photo`
- `camera->Capture()`
- `camera->Explain()`

So the minimal reliable solution is a hybrid:

- device-side MCP tool for capture and upload
- PC-side HTTP face recognition service for matching

## Firmware Tool Added

The firmware now exposes:

- `self.face_recognition.set_service_url`
- `self.face_recognition.get_service_url`
- `self.face_recognition.identify_current_person`

Use it when the user asks things like:

- `看看目前是谁`
- `看看现在是谁在前面`
- `识别一下这个人`

The tool captures a photo and sends it to the same external image analysis URL that the camera already uses.
If a dedicated face recognition service URL is configured, it temporarily switches to that local PC endpoint only for the face recognition call, then restores the previous camera explain URL.

## PC Service

See:

- `tools/face_recognition_service/README.md`

The service listens on:

- `POST /recognize`

and compares against local images under:

- `tools/face_recognition_service/photos/`
