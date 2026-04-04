import base64
import logging
import shutil
import time
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Optional

import cv2
import numpy as np
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field

from . import config
from .face_backend import InsightFaceBackend
from .name_utils import resolve_canonical_name
from .photo_processor import PhotoProcessor
from .vector_store import VectorStore


logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(name)s %(levelname)s %(message)s",
)
logger = logging.getLogger("rig_dog_face_api")

INVALID_FILENAME_CHARS = '<>:"/\\|?*'
PHOTO_SUFFIXES = (".jpg", ".jpeg", ".png", ".bmp", ".webp")


backend: Optional[InsightFaceBackend] = None
vector_store: Optional[VectorStore] = None
photo_processor: Optional[PhotoProcessor] = None
started_at: float = 0.0


class RecognizeRequest(BaseModel):
    image_base64: str
    confidence_threshold: Optional[float] = Field(default=None)


class RecognizeResponse(BaseModel):
    matched: bool
    name: Optional[str]
    confidence: float
    processing_time_ms: int


class EnrollRequest(BaseModel):
    name: str
    image_base64: str


class EnrollResponse(BaseModel):
    success: bool
    name: str
    embedding_saved: bool
    error: Optional[str]


class ReloadRequest(BaseModel):
    force: bool = False


class ReloadResponse(BaseModel):
    success: bool
    loaded: int
    failed: list[dict]
    embeddings_saved: bool
    reload_time_ms: int


class RemoveResponse(BaseModel):
    success: bool
    removed: str
    embeddings_saved: bool


class ListResponse(BaseModel):
    users: list[str]
    count: int


class HealthResponse(BaseModel):
    status: str
    loaded_users: int
    embeddings_file: str
    last_reload: Optional[str]
    uptime_ms: int


class DetectAndEmbedRequest(BaseModel):
    image_base64: str


class DetectAndEmbedResponse(BaseModel):
    success: bool
    faces: list[dict]
    error: Optional[str]
    processing_time_ms: int


class LocateRequest(BaseModel):
    name: str
    image_base64: str
    confidence_threshold: Optional[float] = Field(default=None)


class LocateResponse(BaseModel):
    success: bool
    target_name: str
    matched: bool
    detected_name: Optional[str]
    confidence: float
    bbox: Optional[dict]
    center: Optional[dict]
    offset: Optional[dict]
    image_size: Optional[dict]
    face_ratio: float
    turn_hint: Optional[str]
    distance_hint: Optional[str]
    error: Optional[str]
    processing_time_ms: int


def _normalize_person_name(raw_name: str) -> str:
    name = raw_name.strip()
    for char in INVALID_FILENAME_CHARS:
        name = name.replace(char, "_")
    if not name:
        raise HTTPException(status_code=400, detail="name cannot be empty")
    return name


def _photo_library_names() -> list[str]:
    if photo_processor is None:
        return []
    return photo_processor.get_person_names()


def _vector_library_names() -> list[str]:
    if vector_store is None:
        return []
    return vector_store.list_all()


def _resolve_library_name(raw_name: str) -> str:
    normalized_name = _normalize_person_name(raw_name)
    if not config.PHONETIC_ALIAS_MATCH_ENABLED:
        return normalized_name

    photo_names = _photo_library_names()
    resolved_photo_name = resolve_canonical_name(
        normalized_name,
        photo_names,
        similarity_threshold=config.PHONETIC_ALIAS_MIN_SIMILARITY,
    )
    if resolved_photo_name in photo_names:
        return resolved_photo_name

    vector_names = _vector_library_names()
    resolved_vector_name = resolve_canonical_name(
        normalized_name,
        vector_names,
        similarity_threshold=config.PHONETIC_ALIAS_MIN_SIMILARITY,
    )
    if resolved_vector_name in vector_names:
        return resolved_vector_name

    return normalized_name


def _decode_base64_image(image_base64: str) -> np.ndarray:
    payload = image_base64
    if "," in payload and payload.lstrip().startswith("data:"):
        payload = payload.split(",", 1)[1]

    try:
        binary = base64.b64decode(payload, validate=False)
    except Exception as exc:
        raise HTTPException(status_code=400, detail=f"invalid base64 image: {exc}") from exc

    array = np.frombuffer(binary, dtype=np.uint8)
    image = cv2.imdecode(array, cv2.IMREAD_COLOR)
    if image is None:
        raise HTTPException(status_code=400, detail="failed to decode image")
    return image


def _photo_path_for_name(name: str) -> Path:
    person_folder = config.PHOTOS_FOLDER / name
    person_folder.mkdir(parents=True, exist_ok=True)
    next_index = 1
    for photo_path in person_folder.iterdir():
        if not photo_path.is_file() or photo_path.suffix.lower() not in PHOTO_SUFFIXES:
            continue
        if photo_path.stem.isdigit():
            next_index = max(next_index, int(photo_path.stem) + 1)
    return person_folder / f"{next_index}.jpg"


def _migrate_legacy_library_photos(name: str) -> None:
    for suffix in PHOTO_SUFFIXES:
        legacy_path = config.PHOTOS_FOLDER / f"{name}{suffix}"
        if not legacy_path.exists() or not legacy_path.is_file():
            continue
        target_path = _photo_path_for_name(name)
        legacy_path.replace(target_path.with_suffix(legacy_path.suffix.lower()))


def _save_library_photo(name: str, image: np.ndarray) -> Path:
    _migrate_legacy_library_photos(name)
    success, encoded = cv2.imencode(".jpg", image)
    if not success:
        raise HTTPException(status_code=500, detail="failed to encode photo for storage")
    photo_path = _photo_path_for_name(name)
    photo_path.write_bytes(encoded.tobytes())
    return photo_path


def _rebuild_person_embeddings(name: str) -> tuple[bool, Optional[str]]:
    if photo_processor is None or vector_store is None:
        return False, "service not ready"

    rebuild = photo_processor.process_person_photos(
        name,
        strategy=config.MULTIPLE_FACES_STRATEGY,
    )
    if not rebuild["success"]:
        return False, rebuild["error"] or "failed to rebuild face library"
    if not vector_store.save_to_json():
        return False, "failed to save embedding store"
    return True, None


def _append_person_sample(name: str, image: np.ndarray) -> tuple[bool, Optional[str]]:
    canonical_name = _resolve_library_name(name)
    _save_library_photo(canonical_name, image)
    if vector_store is not None and canonical_name != name:
        vector_store.remove(name)
    return _rebuild_person_embeddings(canonical_name)


def _delete_library_photos(name: str) -> None:
    person_folder = config.PHOTOS_FOLDER / name
    if person_folder.exists() and person_folder.is_dir():
        shutil.rmtree(person_folder)
    for suffix in PHOTO_SUFFIXES:
        photo_path = config.PHOTOS_FOLDER / f"{name}{suffix}"
        if photo_path.exists():
            photo_path.unlink()


def _face_position_payload(image: np.ndarray, face: dict) -> dict:
    image_h, image_w = image.shape[:2]
    bbox = face["bbox"]
    center_x = bbox["x"] + bbox["w"] / 2.0
    center_y = bbox["y"] + bbox["h"] / 2.0
    offset_x = 0.0
    offset_y = 0.0
    if image_w > 0:
        offset_x = (center_x - (image_w / 2.0)) / (image_w / 2.0)
    if image_h > 0:
        offset_y = (center_y - (image_h / 2.0)) / (image_h / 2.0)

    face_ratio = 0.0
    if image_w > 0 and image_h > 0:
        face_ratio = (bbox["w"] * bbox["h"]) / float(image_w * image_h)

    if offset_x < -0.12:
        turn_hint = "left"
    elif offset_x > 0.12:
        turn_hint = "right"
    else:
        turn_hint = "center"

    if face_ratio < 0.07:
        distance_hint = "forward"
    elif face_ratio > 0.18:
        distance_hint = "backward"
    else:
        distance_hint = "hold"

    return {
        "bbox": bbox,
        "center": {
            "x": int(round(center_x)),
            "y": int(round(center_y)),
        },
        "offset": {
            "x": round(float(offset_x), 4),
            "y": round(float(offset_y), 4),
        },
        "image_size": {
            "w": image_w,
            "h": image_h,
        },
        "face_ratio": round(float(face_ratio), 6),
        "turn_hint": turn_hint,
        "distance_hint": distance_hint,
    }


@asynccontextmanager
async def lifespan(app: FastAPI):
    del app
    global backend, vector_store, photo_processor, started_at

    config.ensure_directories()
    started_at = time.time()
    backend = InsightFaceBackend()
    vector_store = VectorStore(config.EMBEDDINGS_JSON)
    photo_processor = PhotoProcessor(config.PHOTOS_FOLDER, backend, vector_store)
    migrated = photo_processor.migrate_legacy_photos()
    vector_store.load_from_json()
    if migrated:
        logger.info("detected %s migrated legacy photos; run /reload to rebuild embeddings", len(migrated))
    logger.info("service ready with %s users", len(vector_store.vectors))
    yield


app = FastAPI(
    title="Rig Dog Face Recognition API",
    version="1.0.0",
    lifespan=lifespan,
)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/")
async def root() -> dict:
    return {
        "service": "Rig Dog Face Recognition API",
        "status": "running",
        "port": config.PORT,
    }


@app.get("/health", response_model=HealthResponse)
async def health() -> HealthResponse:
    if vector_store is None:
        raise HTTPException(status_code=503, detail="service not ready")
    return HealthResponse(
        status="ok",
        loaded_users=len(vector_store.vectors),
        embeddings_file=str(config.EMBEDDINGS_JSON),
        last_reload=vector_store.metadata.get("last_updated"),
        uptime_ms=int((time.time() - started_at) * 1000),
    )


@app.get("/list", response_model=ListResponse)
async def list_users() -> ListResponse:
    if vector_store is None:
        raise HTTPException(status_code=503, detail="service not ready")
    users = sorted(vector_store.list_all(), key=str.casefold)
    return ListResponse(users=users, count=len(users))


@app.post("/recognize", response_model=RecognizeResponse)
async def recognize(request: RecognizeRequest) -> RecognizeResponse:
    if backend is None or vector_store is None:
        raise HTTPException(status_code=503, detail="service not ready")

    started = time.time()
    image = _decode_base64_image(request.image_base64)
    result = backend.extract_from_image(
        image,
        strategy=config.MULTIPLE_FACES_STRATEGY,
    )
    if not result["success"]:
        return RecognizeResponse(
            matched=False,
            name=None,
            confidence=0.0,
            processing_time_ms=int((time.time() - started) * 1000),
        )

    threshold = request.confidence_threshold or config.SIMILARITY_THRESHOLD
    match = vector_store.search(result["embedding"], threshold=threshold)
    resolved_match_name = None
    if match["matched"] and match["name"]:
        resolved_match_name = _resolve_library_name(str(match["name"]))
        if resolved_match_name != match["name"]:
            logger.info("resolved recognized alias '%s' -> '%s'", match["name"], resolved_match_name)
            if vector_store.remove(str(match["name"])):
                saved, error = _rebuild_person_embeddings(resolved_match_name)
                if not saved:
                    logger.warning("failed to reconcile recognized alias '%s': %s", resolved_match_name, error)

        if config.AUTO_APPEND_RECOGNIZED_PHOTOS and photo_processor is not None:
            if float(match["confidence"]) >= config.AUTO_APPEND_MIN_CONFIDENCE:
                appended, append_error = _append_person_sample(resolved_match_name, image)
                if appended:
                    logger.info(
                        "appended recognized sample for '%s' at confidence %.3f",
                        resolved_match_name,
                        float(match["confidence"]),
                    )
                else:
                    logger.warning(
                        "failed to append recognized sample for '%s': %s",
                        resolved_match_name,
                        append_error,
                    )
            else:
                logger.info(
                    "skipped auto-append for '%s': confidence %.3f is below threshold %.3f",
                    resolved_match_name,
                    float(match["confidence"]),
                    config.AUTO_APPEND_MIN_CONFIDENCE,
                )

    return RecognizeResponse(
        matched=match["matched"],
        name=resolved_match_name,
        confidence=float(match["confidence"]),
        processing_time_ms=int((time.time() - started) * 1000),
    )


@app.post("/enroll", response_model=EnrollResponse)
async def enroll(request: EnrollRequest) -> EnrollResponse:
    if backend is None or vector_store is None or photo_processor is None:
        raise HTTPException(status_code=503, detail="service not ready")

    normalized_name = _normalize_person_name(request.name)
    canonical_name = _resolve_library_name(normalized_name)
    if canonical_name != normalized_name:
        logger.info("resolved enrollment alias '%s' -> '%s'", normalized_name, canonical_name)
    image = _decode_base64_image(request.image_base64)

    result = backend.extract_from_image(
        image,
        strategy=config.MULTIPLE_FACES_STRATEGY,
    )
    if not result["success"]:
        return EnrollResponse(
            success=False,
            name=canonical_name,
            embedding_saved=False,
            error=result["error"],
        )

    saved, error_message = _append_person_sample(canonical_name, image)
    return EnrollResponse(
        success=saved,
        name=canonical_name,
        embedding_saved=saved,
        error=error_message,
    )


@app.post("/reload", response_model=ReloadResponse)
async def reload_embeddings(request: ReloadRequest = ReloadRequest()) -> ReloadResponse:
    del request
    if photo_processor is None or vector_store is None:
        raise HTTPException(status_code=503, detail="service not ready")

    started = time.time()
    vector_store.clear()
    result = photo_processor.process_all_photos(strategy=config.MULTIPLE_FACES_STRATEGY)
    saved = vector_store.save_to_json()
    return ReloadResponse(
        success=True,
        loaded=int(result.get("users", result["success"])),
        failed=result["failed"],
        embeddings_saved=saved,
        reload_time_ms=int((time.time() - started) * 1000),
    )


@app.delete("/remove/{name}", response_model=RemoveResponse)
async def remove(name: str) -> RemoveResponse:
    if vector_store is None:
        raise HTTPException(status_code=503, detail="service not ready")
    normalized_name = _resolve_library_name(name)
    if not vector_store.remove(normalized_name):
        raise HTTPException(status_code=404, detail=f"user '{normalized_name}' not found")
    _delete_library_photos(normalized_name)
    saved = vector_store.save_to_json()
    return RemoveResponse(success=True, removed=normalized_name, embeddings_saved=saved)


@app.post("/detect_and_embed", response_model=DetectAndEmbedResponse)
async def detect_and_embed(request: DetectAndEmbedRequest) -> DetectAndEmbedResponse:
    if backend is None:
        raise HTTPException(status_code=503, detail="service not ready")
    started = time.time()
    result = backend.extract_from_base64(
        request.image_base64,
        strategy=config.MULTIPLE_FACES_STRATEGY,
    )
    if not result["success"]:
        return DetectAndEmbedResponse(
            success=False,
            faces=[],
            error=result["error"],
            processing_time_ms=int((time.time() - started) * 1000),
        )
    return DetectAndEmbedResponse(
        success=True,
        faces=[{
            "bbox": result["face"]["bbox"],
            "landmarks": result["face"]["landmarks"],
            "confidence": result["face"]["confidence"],
            "embedding": result["embedding"],
        }],
        error=None,
        processing_time_ms=int((time.time() - started) * 1000),
    )


@app.post("/locate", response_model=LocateResponse)
async def locate_person(request: LocateRequest) -> LocateResponse:
    if backend is None or vector_store is None:
        raise HTTPException(status_code=503, detail="service not ready")

    started = time.time()
    normalized_name = _resolve_library_name(request.name)
    if normalized_name not in vector_store.vectors:
        return LocateResponse(
            success=False,
            target_name=normalized_name,
            matched=False,
            detected_name=None,
            confidence=0.0,
            bbox=None,
            center=None,
            offset=None,
            image_size=None,
            face_ratio=0.0,
            turn_hint=None,
            distance_hint=None,
            error="target not enrolled",
            processing_time_ms=int((time.time() - started) * 1000),
        )

    image = _decode_base64_image(request.image_base64)
    result = backend.extract_from_image(
        image,
        strategy=config.MULTIPLE_FACES_STRATEGY,
    )
    if not result["success"]:
        return LocateResponse(
            success=False,
            target_name=normalized_name,
            matched=False,
            detected_name=None,
            confidence=0.0,
            bbox=None,
            center=None,
            offset=None,
            image_size=None,
            face_ratio=0.0,
            turn_hint=None,
            distance_hint=None,
            error=result["error"],
            processing_time_ms=int((time.time() - started) * 1000),
        )

    threshold = request.confidence_threshold or config.SIMILARITY_THRESHOLD
    face_payload = _face_position_payload(image, result["face"])
    target_confidence = vector_store.score_name(normalized_name, result["embedding"])
    best_match = vector_store.search(result["embedding"], threshold=-1.0)
    detected_name = best_match.get("candidate_name")
    if isinstance(detected_name, str) and detected_name:
        detected_name = _resolve_library_name(detected_name)
    matched = target_confidence is not None and target_confidence >= threshold

    return LocateResponse(
        success=True,
        target_name=normalized_name,
        matched=matched,
        detected_name=detected_name,
        confidence=float(target_confidence or 0.0),
        bbox=face_payload["bbox"],
        center=face_payload["center"],
        offset=face_payload["offset"],
        image_size=face_payload["image_size"],
        face_ratio=float(face_payload["face_ratio"]),
        turn_hint=face_payload["turn_hint"],
        distance_hint=face_payload["distance_hint"],
        error=None,
        processing_time_ms=int((time.time() - started) * 1000),
    )
