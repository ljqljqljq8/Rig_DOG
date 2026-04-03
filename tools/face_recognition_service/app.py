from __future__ import annotations

import io
import json
import logging
import os
from pathlib import Path
from typing import List

import cv2
import numpy as np
from fastapi import FastAPI, File, Form, HTTPException, UploadFile
from PIL import Image


logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
LOGGER = logging.getLogger("face_recognition_service")

BASE_DIR = Path(__file__).resolve().parent
PHOTOS_DIR = Path(os.getenv("FACE_DB_DIR", BASE_DIR / "photos"))
DATA_DIR = Path(os.getenv("FACE_DATA_DIR", BASE_DIR / "data"))
MODEL_PATH = DATA_DIR / "lbph_model.yml"
LABELS_PATH = DATA_DIR / "labels.json"
MATCH_THRESHOLD = float(os.getenv("FACE_MATCH_THRESHOLD", "70"))
FACE_SIZE = (200, 200)


class FaceDatabase:
    def __init__(self, photos_dir: Path, model_path: Path, labels_path: Path) -> None:
        self.photos_dir = photos_dir
        self.model_path = model_path
        self.labels_path = labels_path
        self.recognizer = cv2.face.LBPHFaceRecognizer_create()
        self.label_to_name: dict[int, str] = {}
        self._loaded = False
        self.detector = cv2.CascadeClassifier(
            str(Path(cv2.data.haarcascades) / "haarcascade_frontalface_default.xml")
        )

    def load(self) -> None:
        if self.model_path.exists() and self.labels_path.exists():
            self.recognizer.read(str(self.model_path))
            raw_labels = json.loads(self.labels_path.read_text(encoding="utf-8"))
            self.label_to_name = {int(k): v for k, v in raw_labels.items()}
            self._loaded = True
            LOGGER.info("Loaded LBPH model with %d labels", len(self.label_to_name))
            return
        self.rebuild()

    def rebuild(self) -> None:
        self.photos_dir.mkdir(parents=True, exist_ok=True)
        self.model_path.parent.mkdir(parents=True, exist_ok=True)

        faces: List[np.ndarray] = []
        labels: List[int] = []
        name_to_label: dict[str, int] = {}
        next_label = 0

        for image_path in sorted(self.photos_dir.rglob("*")):
            if not image_path.is_file() or image_path.suffix.lower() not in {".jpg", ".jpeg", ".png"}:
                continue
            person_name = self._infer_name(image_path)
            face = self._extract_single_face(image_path.read_bytes())
            if face is None:
                LOGGER.warning("Skipping %s because exactly one clear face was not found", image_path)
                continue
            if person_name not in name_to_label:
                name_to_label[person_name] = next_label
                next_label += 1
            faces.append(face)
            labels.append(name_to_label[person_name])

        if not faces:
            self.label_to_name = {}
            self._loaded = False
            if self.model_path.exists():
                self.model_path.unlink()
            self.labels_path.write_text("{}", encoding="utf-8")
            LOGGER.warning("No trainable faces found in %s", self.photos_dir)
            return

        self.recognizer = cv2.face.LBPHFaceRecognizer_create()
        self.recognizer.train(faces, np.array(labels, dtype=np.int32))
        self.recognizer.save(str(self.model_path))
        self.label_to_name = {label: name for name, label in name_to_label.items()}
        self.labels_path.write_text(
            json.dumps({str(k): v for k, v in self.label_to_name.items()}, ensure_ascii=False, indent=2),
            encoding="utf-8",
        )
        self._loaded = True
        LOGGER.info("Rebuilt LBPH model with %d labels and %d samples", len(self.label_to_name), len(faces))

    def people(self) -> List[str]:
        return sorted(self.label_to_name.values())

    def recognize(self, image_bytes: bytes) -> dict:
        if not self._loaded or not self.label_to_name:
            return {
                "success": False,
                "matched": False,
                "name": "Unknown",
                "confidence": 0,
                "message": "The local face database is empty. Add photos and rebuild the local model first.",
            }

        face = self._extract_single_face(image_bytes)
        if face is None:
            return {
                "success": True,
                "matched": False,
                "name": "Unknown",
                "confidence": 0,
                "message": "No clear single face was detected in the current photo.",
            }

        predicted_label, distance = self.recognizer.predict(face)
        predicted_name = self.label_to_name.get(int(predicted_label), "Unknown")
        confidence = round(max(0.0, 100.0 - min(100.0, float(distance))), 2)

        if float(distance) > MATCH_THRESHOLD:
            return {
                "success": True,
                "matched": False,
                "name": "Unknown",
                "confidence": confidence,
                "message": "A face was detected, but it did not match any known person in the local database.",
            }

        return {
            "success": True,
            "matched": True,
            "name": predicted_name,
            "confidence": confidence,
            "message": f"The current person is {predicted_name}.",
        }

    def _extract_single_face(self, image_bytes: bytes) -> np.ndarray | None:
        image = Image.open(io.BytesIO(image_bytes)).convert("RGB")
        frame = cv2.cvtColor(np.array(image), cv2.COLOR_RGB2BGR)
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        faces = self.detector.detectMultiScale(
            gray,
            scaleFactor=1.1,
            minNeighbors=5,
            minSize=(60, 60),
        )
        if len(faces) != 1:
            return None

        x, y, w, h = faces[0]
        cropped = gray[y:y + h, x:x + w]
        normalized = cv2.resize(cropped, FACE_SIZE)
        return normalized

    def _infer_name(self, image_path: Path) -> str:
        if image_path.parent.name != self.photos_dir.name:
            return image_path.parent.name
        return image_path.stem


database = FaceDatabase(PHOTOS_DIR, MODEL_PATH, LABELS_PATH)
app = FastAPI(title="RIG Puppy Face Recognition Service")


@app.on_event("startup")
def startup_event() -> None:
    database.load()


@app.get("/health")
def health() -> dict:
    return {
        "success": True,
        "people": database.people(),
        "known_labels": len(database.label_to_name),
        "photos_dir": str(PHOTOS_DIR),
        "model_path": str(MODEL_PATH),
    }


@app.get("/people")
def list_people() -> dict:
    return {
        "success": True,
        "people": database.people(),
        "known_labels": len(database.label_to_name),
    }


@app.post("/rebuild")
def rebuild_database() -> dict:
    database.rebuild()
    return {
        "success": True,
        "people": database.people(),
        "known_labels": len(database.label_to_name),
    }


@app.post("/recognize")
async def recognize(
    file: UploadFile = File(...),
    question: str = Form(default=""),
) -> dict:
    payload = await file.read()
    if not payload:
        raise HTTPException(status_code=400, detail="Uploaded file is empty")
    LOGGER.info("Recognize request received, question=%s, bytes=%d", question, len(payload))
    result = database.recognize(payload)
    result["question"] = question
    LOGGER.info("Recognize result: %s", result)
    return result
