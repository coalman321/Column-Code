from pathlib import Path

from fastapi import FastAPI
from fastapi.responses import RedirectResponse
from fastapi.staticfiles import StaticFiles

app = FastAPI()

STATIC_DIR = Path(__file__).parent.parent.parent / "web_app" / "build"


@app.get("/")
def root():
    return RedirectResponse(url="/static/")


@app.get("/static")
def static_root():
    return RedirectResponse(url="/static/")


@app.get("/static/index.html")
def static_index():
    return RedirectResponse(url="/static/")


if STATIC_DIR.exists():
    app.mount("/static", StaticFiles(directory=STATIC_DIR, html=True), name="static")
