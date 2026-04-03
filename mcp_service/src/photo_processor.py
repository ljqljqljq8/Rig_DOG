import logging
import time
from pathlib import Path
from typing import Any

import cv2

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

        for photo_path in self._find_photos():
            result = self.process_single_photo(photo_path, strategy=strategy)
            if result["success"]:
                success += 1
            else:
                failed.append({"file": photo_path.name, "error": result["error"]})

        return {
            "success": success,
            "failed": failed,
            "total": success + len(failed),
            "duration_ms": int((time.time() - start) * 1000),
        }

    def process_single_photo(self, file_path: Path, strategy: str | None = None) -> dict[str, Any]:
        strategy = strategy or config.MULTIPLE_FACES_STRATEGY
        image = cv2.imread(str(file_path))
        if image is None:
            return {
                "success": False,
                "name": file_path.stem,
                "embedding": None,
                "error": "failed to read image",
            }

        result = self.backend.extract_from_image(image, strategy=strategy)
        if not result["success"]:
            return {
                "success": False,
                "name": file_path.stem,
                "embedding": None,
                "error": result["error"],
            }

        self.vector_store.add(file_path.stem, result["embedding"])
        return {
            "success": True,
            "name": file_path.stem,
            "embedding": result["embedding"],
            "error": None,
        }

    def get_photo_stats(self) -> dict[str, Any]:
        photos = self._find_photos()
        return {
            "total_photos": len(photos),
            "photos_folder": str(self.photos_folder),
        }

    def _find_photos(self) -> list[Path]:
        if not self.photos_folder.exists():
            return []
        return sorted(
            [
                path for path in self.photos_folder.iterdir()
                if path.is_file() and path.suffix.lower() in IMAGE_EXTENSIONS
            ],
            key=lambda item: item.name.lower(),
        )

