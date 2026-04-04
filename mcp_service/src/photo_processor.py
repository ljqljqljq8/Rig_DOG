import logging
import time
from pathlib import Path
from typing import Any

import cv2
import numpy as np

from . import config


logger = logging.getLogger(__name__)

IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}


class PhotoProcessor:
    def __init__(self, photos_folder: str | Path, backend: Any, vector_store: Any) -> None:
        self.photos_folder = Path(photos_folder)
        self.backend = backend
        self.vector_store = vector_store
        self.photos_folder.mkdir(parents=True, exist_ok=True)

    def process_all_photos(self, strategy: str | None = None) -> dict[str, Any]:
        start = time.time()
        strategy = strategy or config.MULTIPLE_FACES_STRATEGY
        success = 0
        failed: list[dict[str, str]] = []
        photos = self._find_photos()
        grouped = self._group_photos_by_person(photos)

        for name, photo_paths in grouped.items():
            result = self.process_person_photos(name, photo_paths=photo_paths, strategy=strategy)
            if result["success"]:
                success += 1
            failed.extend(result["failed"])

        return {
            "success": success,
            "failed": failed,
            "total": len(photos),
            "users": len(grouped),
            "duration_ms": int((time.time() - start) * 1000),
        }

    def process_person_photos(
        self,
        name: str,
        photo_paths: list[Path] | None = None,
        strategy: str | None = None,
    ) -> dict[str, Any]:
        strategy = strategy or config.MULTIPLE_FACES_STRATEGY
        photo_paths = photo_paths or self._find_person_photos(name)
        embeddings: list[np.ndarray] = []
        failed: list[dict[str, str]] = []

        for photo_path in photo_paths:
            result = self.process_single_photo(photo_path, strategy=strategy, person_name=name)
            if result["success"]:
                embeddings.append(np.asarray(result["embedding"], dtype=np.float32))
            else:
                failed.append({"file": result["file"], "error": result["error"]})

        if not embeddings:
            self.vector_store.remove(name)
            return {
                "success": False,
                "name": name,
                "embedding": None,
                "failed": failed,
                "samples_loaded": 0,
                "error": "no valid photos for person",
            }

        centroid = np.mean(np.stack(embeddings, axis=0), axis=0)
        if not self.vector_store.add(name, centroid):
            return {
                "success": False,
                "name": name,
                "embedding": None,
                "failed": failed,
                "samples_loaded": len(embeddings),
                "error": "failed to store aggregated embedding",
            }
        return {
            "success": True,
            "name": name,
            "embedding": centroid.tolist(),
            "failed": failed,
            "samples_loaded": len(embeddings),
            "error": None,
        }

    def process_single_photo(
        self,
        file_path: Path,
        strategy: str | None = None,
        person_name: str | None = None,
    ) -> dict[str, Any]:
        strategy = strategy or config.MULTIPLE_FACES_STRATEGY
        image = self._read_image(file_path)
        name = person_name or self._person_name_for_path(file_path)
        display_path = self._display_path(file_path)
        if image is None:
            return {
                "success": False,
                "name": name,
                "file": display_path,
                "embedding": None,
                "error": "failed to read image",
            }

        result = self.backend.extract_from_image(image, strategy=strategy)
        if not result["success"]:
            return {
                "success": False,
                "name": name,
                "file": display_path,
                "embedding": None,
                "error": result["error"],
            }

        return {
            "success": True,
            "name": name,
            "file": display_path,
            "embedding": result["embedding"],
            "error": None,
        }

    def get_photo_stats(self) -> dict[str, Any]:
        photos = self._find_photos()
        return {
            "total_photos": len(photos),
            "total_users": len(self._group_photos_by_person(photos)),
            "photos_folder": str(self.photos_folder),
        }

    def get_person_names(self) -> list[str]:
        return sorted(self._group_photos_by_person(self._find_photos()).keys(), key=str.casefold)

    def _find_photos(self) -> list[Path]:
        if not self.photos_folder.exists():
            return []
        return sorted(
            [
                path for path in self.photos_folder.rglob("*")
                if path.is_file() and path.suffix.lower() in IMAGE_EXTENSIONS
            ],
            key=lambda item: str(item.relative_to(self.photos_folder)).lower(),
        )

    def _find_person_photos(self, name: str) -> list[Path]:
        return [
            photo_path for photo_path in self._find_photos()
            if self._person_name_for_path(photo_path) == name
        ]

    def _group_photos_by_person(self, photo_paths: list[Path]) -> dict[str, list[Path]]:
        grouped: dict[str, list[Path]] = {}
        for photo_path in photo_paths:
            grouped.setdefault(self._person_name_for_path(photo_path), []).append(photo_path)
        return grouped

    def _person_name_for_path(self, file_path: Path) -> str:
        relative_path = file_path.relative_to(self.photos_folder)
        if len(relative_path.parts) > 1:
            return relative_path.parts[0]
        return file_path.stem

    def _display_path(self, file_path: Path) -> str:
        return str(file_path.relative_to(self.photos_folder))

    def _read_image(self, file_path: Path) -> Any:
        try:
            binary = file_path.read_bytes()
        except OSError:
            return None
        array = np.frombuffer(binary, dtype=np.uint8)
        return cv2.imdecode(array, cv2.IMREAD_COLOR)

