import base64
import logging
from typing import Any

import cv2
import numpy as np

from . import config


logger = logging.getLogger(__name__)


class InsightFaceBackend:
    def __init__(self) -> None:
        try:
            from insightface.app import FaceAnalysis
        except ImportError as exc:
            raise RuntimeError(
                "insightface is not installed. Create the venv and install requirements.txt first."
            ) from exc

        config.ensure_directories()
        self._app = FaceAnalysis(
            name=config.INSIGHTFACE_MODEL,
            root=str(config.MODEL_CACHE_DIR),
            providers=config.INSIGHTFACE_PROVIDERS,
        )
        self._app.prepare(ctx_id=0, det_size=config.DETECTION_SIZE)

    def extract_from_base64(self, image_base64: str, strategy: str = "largest") -> dict[str, Any]:
        if "," in image_base64 and image_base64.lstrip().startswith("data:"):
            image_base64 = image_base64.split(",", 1)[1]

        try:
            binary = base64.b64decode(image_base64, validate=False)
        except Exception as exc:
            return self._failure(f"invalid base64 image: {exc}")

        array = np.frombuffer(binary, dtype=np.uint8)
        image = cv2.imdecode(array, cv2.IMREAD_COLOR)
        if image is None:
            return self._failure("failed to decode image")
        return self.extract_from_image(image, strategy=strategy)

    def extract_from_image(self, image: np.ndarray, strategy: str = "largest") -> dict[str, Any]:
        result = self.extract_faces_from_image(image)
        if not result["success"]:
            return self._failure(result["error"])

        faces = result["faces"]
        if len(faces) > 1 and strategy == "error":
            return self._failure(f"multiple faces detected ({len(faces)})")

        if strategy == "largest":
            selected = max(faces, key=lambda face: self._bbox_area(face["face"]["bbox"]))
        else:
            selected = faces[0]

        embedding = selected["embedding"]
        bbox = selected["face"]["bbox"]
        landmarks = selected["face"]["landmarks"]
        confidence = selected["face"]["confidence"]

        return {
            "success": True,
            "embedding": embedding,
            "face": {
                "bbox": bbox,
                "landmarks": landmarks,
                "confidence": confidence,
            },
            "error": None,
        }

    def extract_faces_from_image(self, image: np.ndarray) -> dict[str, Any]:
        faces = self._app.get(image)
        if not faces:
            return {
                "success": False,
                "faces": [],
                "error": "no face detected",
            }

        extracted_faces: list[dict[str, Any]] = []
        for face in faces:
            extracted_faces.append(
                {
                    "embedding": self._normalized_embedding(face),
                    "face": {
                        "bbox": self._bbox_dict(face),
                        "landmarks": self._landmarks(face),
                        "confidence": float(getattr(face, "det_score", 0.0)),
                    },
                }
            )

        return {
            "success": True,
            "faces": extracted_faces,
            "error": None,
        }

    def _normalized_embedding(self, face: Any) -> list[float]:
        embedding = getattr(face, "normed_embedding", None)
        if embedding is None:
            embedding = getattr(face, "embedding", None)
        if embedding is None:
            raise RuntimeError("embedding missing from insightface result")

        vector = np.asarray(embedding, dtype=np.float32).reshape(-1)
        norm = float(np.linalg.norm(vector))
        if norm > 0:
            vector = vector / norm
        return vector.astype(np.float32).tolist()

    def _bbox_dict(self, face: Any) -> dict[str, int]:
        bbox = np.asarray(face.bbox, dtype=np.float32).reshape(-1)
        x1, y1, x2, y2 = bbox[:4]
        return {
            "x": int(round(x1)),
            "y": int(round(y1)),
            "w": int(round(max(0.0, x2 - x1))),
            "h": int(round(max(0.0, y2 - y1))),
        }

    def _landmarks(self, face: Any) -> list[list[float]]:
        kps = getattr(face, "kps", None)
        if kps is None:
            return []
        points = np.asarray(kps, dtype=np.float32)
        return [[float(point[0]), float(point[1])] for point in points]

    def _face_area(self, face: Any) -> float:
        bbox = np.asarray(face.bbox, dtype=np.float32).reshape(-1)
        return float(max(0.0, bbox[2] - bbox[0]) * max(0.0, bbox[3] - bbox[1]))

    def _bbox_area(self, bbox: dict[str, int]) -> float:
        return float(max(0, bbox["w"]) * max(0, bbox["h"]))

    def _failure(self, error: str) -> dict[str, Any]:
        logger.warning("face extraction failed: %s", error)
        return {
            "success": False,
            "embedding": None,
            "face": None,
            "error": error,
        }
