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
        migrated = self.migrate_legacy_photos()

        for photo_path in self._find_photos():
            result = self.process_single_photo(photo_path, strategy=strategy)
            if result["success"]:
                success += 1
            else:
                failed.append({"file": self._relative_photo_path(photo_path), "error": result["error"]})

        return {
            "success": success,
            "failed": failed,
            "total": success + len(failed),
            "migrated": len(migrated),
            "duration_ms": int((time.time() - start) * 1000),
        }

    def process_single_photo(self, file_path: Path, strategy: str | None = None) -> dict[str, Any]:
        strategy = strategy or config.MULTIPLE_FACES_STRATEGY
        person_name = self._person_name_for_photo(file_path)
        image = self._read_image(file_path)
        if image is None:
            return {
                "success": False,
                "name": person_name,
                "embedding": None,
                "error": "failed to read image",
            }

        result = self.backend.extract_from_image(image, strategy=strategy)
        if not result["success"]:
            return {
                "success": False,
                "name": person_name,
                "embedding": None,
                "error": result["error"],
            }

        self.vector_store.add(person_name, result["embedding"])
        return {
            "success": True,
            "name": person_name,
            "embedding": result["embedding"],
            "error": None,
        }

    def get_photo_stats(self) -> dict[str, Any]:
        photos = self._find_photos()
        return {
            "total_photos": len(photos),
            "total_people": len({self._person_name_for_photo(path) for path in photos}),
            "photos_folder": str(self.photos_folder),
        }

    def migrate_legacy_photos(self) -> list[dict[str, str]]:
        migrated: list[dict[str, str]] = []
        if not self.photos_folder.exists():
            return migrated

        for path in sorted(self.photos_folder.iterdir(), key=lambda item: item.name.lower()):
            if not path.is_file() or path.suffix.lower() not in IMAGE_EXTENSIONS:
                continue

            person_name = path.stem
            person_folder = self.photos_folder / person_name
            person_folder.mkdir(parents=True, exist_ok=True)

            target_path = self._unique_path(person_folder / path.name)
            path.replace(target_path)
            migrated.append(
                {
                    "from": path.name,
                    "to": self._relative_photo_path(target_path),
                }
            )

        if migrated:
            logger.info("migrated %s legacy photos into person folders", len(migrated))
        return migrated

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

    def _person_name_for_photo(self, file_path: Path) -> str:
        relative_path = file_path.relative_to(self.photos_folder)
        if len(relative_path.parts) >= 2:
            return relative_path.parts[0]
        return file_path.stem

    def _relative_photo_path(self, file_path: Path) -> str:
        return str(file_path.relative_to(self.photos_folder)).replace("\\", "/")

    def _unique_path(self, file_path: Path) -> Path:
        if not file_path.exists():
            return file_path

        stem = file_path.stem
        suffix = file_path.suffix
        counter = 2
        while True:
            candidate = file_path.with_name(f"{stem}_{counter}{suffix}")
            if not candidate.exists():
                return candidate
            counter += 1

    def _read_image(self, file_path: Path) -> Any:
        try:
            binary = np.fromfile(str(file_path), dtype=np.uint8)
        except OSError:
            return None

        if binary.size == 0:
            return None
        return cv2.imdecode(binary, cv2.IMREAD_COLOR)
