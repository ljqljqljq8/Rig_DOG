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
        self.vectors: dict[str, list[np.ndarray]] = {}
        self.metadata: dict[str, Any] = {
            "version": "2.0",
            "last_updated": None,
            "total_users": 0,
            "total_embeddings": 0,
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

        self.vectors = {}
        for name, raw_vectors in payload.items():
            normalized_vectors = self._load_vectors(name, raw_vectors)
            if normalized_vectors:
                self.vectors[name] = normalized_vectors

        self.metadata["version"] = "2.0"
        self.metadata["total_users"] = len(self.vectors)
        self.metadata["total_embeddings"] = self.total_embeddings()
        return True

    def save_to_json(self) -> bool:
        self.metadata["last_updated"] = datetime.now().isoformat()
        self.metadata["total_users"] = len(self.vectors)
        self.metadata["total_embeddings"] = self.total_embeddings()
        self.metadata["version"] = "2.0"

        payload = {
            name: [vector.astype(np.float32).tolist() for vector in vectors]
            for name, vectors in self.vectors.items()
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
        self.vectors.setdefault(name, []).append(self._normalize(array))
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

        query = self._prepare_query(query_vector)
        if query is None:
            return {"matched": False, "name": None, "confidence": 0.0}

        best_name = None
        best_similarity = -1.0
        for name, vectors in self.vectors.items():
            if not vectors:
                continue
            similarity = self._best_similarity(vectors, query)
            if similarity > best_similarity:
                best_similarity = similarity
                best_name = name

        if best_name is None:
            return {"matched": False, "name": None, "confidence": 0.0}

        if best_similarity >= threshold:
            return {
                "matched": True,
                "name": best_name,
                "confidence": best_similarity,
                "candidate_name": best_name,
            }
        return {
            "matched": False,
            "name": None,
            "confidence": best_similarity,
            "candidate_name": best_name,
        }

    def score_name(self, name: str, query_vector: list[float] | np.ndarray) -> float | None:
        vectors = self.vectors.get(name)
        if not vectors:
            return None

        query = self._prepare_query(query_vector)
        if query is None:
            return None
        return self._best_similarity(vectors, query)

    def total_embeddings(self) -> int:
        return sum(len(vectors) for vectors in self.vectors.values())

    def _normalize(self, vector: np.ndarray) -> np.ndarray:
        norm = float(np.linalg.norm(vector))
        if norm > 0:
            return vector / norm
        return vector

    def _prepare_query(self, query_vector: list[float] | np.ndarray) -> np.ndarray | None:
        query = np.asarray(query_vector, dtype=np.float32).reshape(-1)
        if query.size != self.dimension:
            return None
        return self._normalize(query)

    def _best_similarity(self, vectors: list[np.ndarray], query: np.ndarray) -> float:
        return max(float(np.dot(vector, query)) for vector in vectors)

    def _load_vectors(self, name: str, raw_vectors: Any) -> list[np.ndarray]:
        if not isinstance(raw_vectors, list) or not raw_vectors:
            logger.warning("skip invalid vectors payload for %s", name)
            return []

        if isinstance(raw_vectors[0], (int, float)):
            raw_vectors = [raw_vectors]

        normalized_vectors: list[np.ndarray] = []
        for index, raw_vector in enumerate(raw_vectors, start=1):
            array = np.asarray(raw_vector, dtype=np.float32).reshape(-1)
            if array.size != self.dimension:
                logger.warning(
                    "skip invalid vector dimension for %s #%s: %s",
                    name,
                    index,
                    array.size,
                )
                continue
            normalized_vectors.append(self._normalize(array))
        return normalized_vectors
