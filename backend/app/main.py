import asyncio
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI
from fastapi.responses import RedirectResponse
from fastapi.staticfiles import StaticFiles

from app.clients import router as clients_router
from app.colors import router as colors_router
from app.firmware import router as firmware_router
from app.logs import router as logs_router
from app.presets import router as presets_router
from app.ota import router as ota_router
import app.mqtt as mqtt_manager


@asynccontextmanager
async def lifespan(app: FastAPI):
    from app.db import init_db
    from app.colors import load_from_db as load_colors
    from app.clients import load_from_db as load_clients
    from app.presets import load_from_db as load_presets
    init_db()
    load_colors()
    load_clients()
    load_presets()
    task = asyncio.create_task(mqtt_manager.run())
    yield
    task.cancel()
    try:
        await task
    except asyncio.CancelledError:
        pass


app = FastAPI(lifespan=lifespan)
app.include_router(clients_router)
app.include_router(colors_router)
app.include_router(presets_router)
app.include_router(firmware_router)
app.include_router(ota_router)
app.include_router(logs_router)

STATIC_DIR = Path(__file__).parent.parent.parent / "web_app" / "build"


@app.get("/health")
def health():
    return {"ok": True}


@app.get("/")
def root():
    return RedirectResponse(url="/static/")


@app.get("/static/index.html")
def static_index():
    return RedirectResponse(url="/static/")


if STATIC_DIR.exists():
    app.mount("/static", StaticFiles(directory=STATIC_DIR, html=True), name="static")
