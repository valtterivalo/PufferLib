"""Model loading helpers backed by the existing OSRS model exporter."""

from __future__ import annotations

import sys
from pathlib import Path

from .config import INDEX_MODELS
from .store import RcCacheStore

_PIPELINE = Path(__file__).resolve().parents[1]
if str(_PIPELINE) not in sys.path:
    sys.path.insert(0, str(_PIPELINE))

from export_models import (  # noqa: E402
    ModelData,
    _merge_models as merge_models,
    decode_model,
    expand_model,
    hsl15_to_rgb,
    write_models_binary,
)


def load_model_bytes(store: RcCacheStore, model_id: int) -> bytes | None:
    return store.read_container(INDEX_MODELS, model_id)


def load_model(store: RcCacheStore, model_id: int) -> ModelData | None:
    data = load_model_bytes(store, model_id)
    if data is None:
        return None
    return decode_model(model_id, data)
