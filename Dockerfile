# ── Stage 1: build the Svelte frontend ───────────────────────────────────
FROM node:22-alpine AS web-builder
WORKDIR /build
COPY web_app/package.json web_app/package-lock.json ./
RUN npm ci
COPY web_app/ ./
RUN rm -rf build .svelte-kit node_modules/.vite
RUN npm run build

# ── Stage 2: Python backend + built frontend ──────────────────────────────
FROM python:3.12-slim

WORKDIR /app
COPY backend/requirements.docker.txt requirements.txt
RUN pip install --no-cache-dir -r requirements.txt

COPY backend/ backend/
COPY --from=web-builder /build/build web_app/build/

# firmware/app.bin is served from backend/firmware/app.bin at runtime.
# Mount a volume or copy the binary in before running if OTA is needed:
#   docker run -v /path/to/app.bin:/app/backend/firmware/app.bin ...
RUN mkdir -p backend/firmware backend/data

WORKDIR /app/backend
EXPOSE 8000
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]
