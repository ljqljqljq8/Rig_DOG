import tempfile
import unittest
from pathlib import Path

import numpy as np

from src.app import _build_recognition_matches
from src.vector_store import VectorStore


def _unit_vector(index: int, dimension: int = 512) -> np.ndarray:
    vector = np.zeros(dimension, dtype=np.float32)
    vector[index] = 1.0
    return vector


class MultiFaceRecognitionTests(unittest.TestCase):
    def test_returns_all_faces_in_left_to_right_order(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            store = VectorStore(Path(temp_dir) / "embeddings.json")
            store.add("alice", _unit_vector(0))
            store.add("bob", _unit_vector(1))

            extracted_faces = [
                {
                    "embedding": _unit_vector(1).tolist(),
                    "face": {
                        "bbox": {"x": 620, "y": 120, "w": 140, "h": 140},
                        "landmarks": [],
                        "confidence": 0.97,
                    },
                },
                {
                    "embedding": _unit_vector(0).tolist(),
                    "face": {
                        "bbox": {"x": 90, "y": 80, "w": 180, "h": 180},
                        "landmarks": [],
                        "confidence": 0.98,
                    },
                },
            ]

            matches = _build_recognition_matches(
                extracted_faces=extracted_faces,
                store=store,
                threshold=0.45,
            )

            self.assertEqual([face["name"] for face in matches], ["alice", "bob"])
            self.assertTrue(all(face["matched"] for face in matches))
            self.assertEqual(len(matches), 2)


if __name__ == "__main__":
    unittest.main()
