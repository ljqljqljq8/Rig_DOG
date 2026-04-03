import json
import logging
from datetime import datetime
from pathlib import Path
from typing import Any

import numpy as np


logger = logging.getLogger(__name__)


class VectorStore:
    def __init__(self, json_path: str | Path, dimension: int = 512) -> None:
        self.json_path = Path(json_path)
        self.dimension = dimension
        self.vectors: dict[str, np.ndarray] = {}
        self.metadata: dict[str, Any] = {
            "version": "1.0",
            "last_updated": None,
            "total_users": 0,
        }
        self.json_path.parent.mkdir(parents=True, exist_ok=True)

    def load_from_json(self) -> bool:
        if not self.json_path.exists():
            return False

        with self.json_path.open("r", encoding="utf-8") as handle:
            payload = json.load(handle)

        metadata = payload.pop("_metadata", None)
        if isinstance(metadata, dict):
            self.metadata.update(metadata)

        self.vectors = {
            name: self._normalize(np.asarray(vector, dtype=np.float32))
            for name, vector in payload.items()
        }
        return True

    def save_to_json(self) -> bool:
        self.metadata["last_updated"] = datetime.now().isoformat()
        self.metadata["total_users"] = len(self.vectors)

        payload = {
            name: vector.astype(np.float32).tolist()
            for name, vector in self.vectors.items()
        }
        payload["_metadata"] = self.metadata

        with self.json_path.open("w", encoding="utf-8") as handle:
            json.dump(payload, handle, indent=2, ensure_ascii=False)
        return True

    def add(self, name: str, vector: list[float] | np.ndarray) -> bool:
        array = np.asarray(vector, dtype=np.float32).reshape(-1)
        if array.size != self.dimension:
            logger.error("invalid vector dimension for %s: %s", name, array.size)
            return False
        self.vectors[name] = self._normalize(array)
        return True

    def remove(self, name: str) -> bool:
        if name not in self.vectors:
            return False
        del self.vectors[name]
        return True

    def list_all(self) -> list[str]:
        return list(self.vectors.keys())

    def clear(self) -> None:
        self.vectors.clear()

    def search(self, query_vector: list[float] | np.ndarray, threshold: float) -> dict[str, Any]:
        if not self.vectors:
            return {"matched": False, "name": None, "confidence": 0.0}

        query = np.asarray(query_vector, dtype=np.float32).reshape(-1)
        if query.size != self.dimension:
            return {"matched": False, "name": None, "confidence": 0.0}
        query = self._normalize(query)

        best_name = None
        best_similarity = -1.0
        for name, vector in self.vectors.items():
            similarity = float(np.dot(vector, query))
            if similarity > best_similarity:
                best_similarity = similarity
                best_name = name

        if best_name is None:
            return {"matched": False, "name": None, "confidence": 0.0}

        if best_similarity >= threshold:
            return {"matched": True, "name": best_name, "confidence": best_similarity}
        return {"matched": False, "name": None, "confidence": best_similarity}

    def _normalize(self, vector: np.ndarray) -> np.ndarray:
        norm = float(np.linalg.norm(vector))
        if norm > 0:
            return vector / norm
        return vector

