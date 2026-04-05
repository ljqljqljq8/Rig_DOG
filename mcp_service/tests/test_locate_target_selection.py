import tempfile
import unittest
from pathlib import Path

import numpy as np

from src.app import _select_locate_candidate
from src.vector_store import VectorStore


def _unit_vector(index: int, dimension: int = 512) -> np.ndarray:
    vector = np.zeros(dimension, dtype=np.float32)
    vector[index] = 1.0
    return vector


class LocateTargetSelectionTests(unittest.TestCase):
    def test_selects_requested_person_in_multi_face_scene(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            store = VectorStore(Path(temp_dir) / "embeddings.json")
            store.add("小乔", _unit_vector(0))
            store.add("大酷盖", _unit_vector(1))

            image = np.zeros((1000, 1000, 3), dtype=np.uint8)
            extracted_faces = [
                {
                    "embedding": _unit_vector(1).tolist(),
                    "face": {
                        "bbox": {"x": 50, "y": 100, "w": 420, "h": 420},
                        "landmarks": [],
                        "confidence": 0.99,
                    },
                },
                {
                    "embedding": _unit_vector(0).tolist(),
                    "face": {
                        "bbox": {"x": 700, "y": 140, "w": 180, "h": 180},
                        "landmarks": [],
                        "confidence": 0.98,
                    },
                },
            ]

            selected = _select_locate_candidate(
                image=image,
                extracted_faces=extracted_faces,
                target_name="小乔",
                store=store,
                threshold=0.45,
            )

            self.assertTrue(selected["matched"])
            self.assertEqual(selected["detected_name"], "小乔")
            self.assertGreater(selected["confidence"], 0.99)
            self.assertGreater(selected["offset"]["x"], 0.3)


if __name__ == "__main__":
    unittest.main()
