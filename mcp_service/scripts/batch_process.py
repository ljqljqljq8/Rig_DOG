#!/usr/bin/env python3
import argparse
import logging
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from src import config
from src.face_backend import InsightFaceBackend
from src.photo_processor import PhotoProcessor
from src.vector_store import VectorStore


logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
)
logger = logging.getLogger("batch_process")


def main() -> int:
    parser = argparse.ArgumentParser(description="Build embeddings from the photos folder.")
    parser.add_argument("--photos", default=str(config.PHOTOS_FOLDER))
    parser.add_argument("--output", default=str(config.EMBEDDINGS_JSON))
    parser.add_argument(
        "--strategy",
        default=config.MULTIPLE_FACES_STRATEGY,
        choices=["largest", "first", "error"],
    )
    args = parser.parse_args()

    config.ensure_directories()

    backend = InsightFaceBackend()
    vector_store = VectorStore(args.output)
    vector_store.clear()

    processor = PhotoProcessor(args.photos, backend, vector_store)
    result = processor.process_all_photos(strategy=args.strategy)
    saved = vector_store.save_to_json()

    logger.info("loaded_users=%s failed_files=%s total_photos=%s saved=%s",
                result["success"], len(result["failed"]), result["total"], saved)

    if result["failed"]:
        for item in result["failed"]:
            logger.warning("%s: %s", item["file"], item["error"])

    return 0 if saved else 1


if __name__ == "__main__":
    raise SystemExit(main())

