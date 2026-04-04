import os
from pathlib import Path


BASE_DIR = Path(__file__).resolve().parent.parent
PHOTOS_FOLDER = Path(os.getenv("PHOTOS_FOLDER", BASE_DIR / "photos"))
EMBEDDINGS_JSON = Path(os.getenv("EMBEDDINGS_JSON", BASE_DIR / "data" / "embeddings.json"))
MODEL_CACHE_DIR = Path(os.getenv("INSIGHTFACE_HOME", BASE_DIR / "models_cache"))

INSIGHTFACE_MODEL = os.getenv("INSIGHTFACE_MODEL", "buffalo_l")
INSIGHTFACE_PROVIDERS = [
    provider.strip()
    for provider in os.getenv("INSIGHTFACE_PROVIDERS", "CPUExecutionProvider").split(",")
    if provider.strip()
]

HOST = os.getenv("HOST", "0.0.0.0")
PORT = int(os.getenv("PORT", "8001"))

SIMILARITY_THRESHOLD = float(os.getenv("SIMILARITY_THRESHOLD", "0.45"))
MULTIPLE_FACES_STRATEGY = os.getenv("MULTIPLE_FACES_STRATEGY", "largest")
DETECTION_SIZE = (
    int(os.getenv("DETECTION_WIDTH", "640")),
    int(os.getenv("DETECTION_HEIGHT", "640")),
)
PHONETIC_ALIAS_MATCH_ENABLED = os.getenv("PHONETIC_ALIAS_MATCH_ENABLED", "1").lower() not in {"0", "false", "no"}
PHONETIC_ALIAS_MIN_SIMILARITY = float(os.getenv("PHONETIC_ALIAS_MIN_SIMILARITY", "0.92"))
AUTO_APPEND_RECOGNIZED_PHOTOS = os.getenv("AUTO_APPEND_RECOGNIZED_PHOTOS", "1").lower() not in {"0", "false", "no"}
AUTO_APPEND_MIN_CONFIDENCE = float(os.getenv("AUTO_APPEND_MIN_CONFIDENCE", "0.60"))


def ensure_directories() -> None:
    PHOTOS_FOLDER.mkdir(parents=True, exist_ok=True)
    EMBEDDINGS_JSON.parent.mkdir(parents=True, exist_ok=True)
    MODEL_CACHE_DIR.mkdir(parents=True, exist_ok=True)

