# Column-Code

Monorepo containing the web app, backend API, and ESP32-C6 firmware.

## Repository Structure

```
column-project/
├── web_app/      # Svelte 5 / SvelteKit frontend
├── backend/      # Python FastAPI backend
└── firmware/     # ESP32-C6 firmware (ESP-IDF)
```

---

## Web App

**Requirements:** Node.js 18+

```bash
cd web_app
npm install
```

### Development

```bash
npm run dev
```

### Production Build

```bash
npm run build
```

The static output is written to `web_app/build/` and served by the backend at `/static/`.

---

## Backend

**Requirements:** Python 3.12+

### Setup

```bash
cd backend
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

### Run (development)

```bash
uvicorn app.main:app --reload
```

The API is available at `http://localhost:8000`.  
The built web app is served at `http://localhost:8000/static/`.

---

## Firmware

**Requirements:** ESP-IDF v5.4.1, targeting ESP32-C6

### Install ESP-IDF

Run the provided setup script from the repo root:

```bash
./firmware/setup_idf.sh
```

Then activate the ESP-IDF environment in your shell:

```bash
. ~/esp/esp-idf/export.sh
```

### Configure

```bash
cd firmware
idf.py set-target esp32c6
idf.py menuconfig
```

Under **Column Firmware Configuration** set:

| Option | Description | Default |
|---|---|---|
| `WIFI_SSID` | WiFi network name | `myssid` |
| `WIFI_PASSWORD` | WiFi password | `mypassword` |
| `SERVER_BASE_URL` | HTTP server base URL | `http://192.168.1.1:8000` |
| `HTTP_POLL_INTERVAL_MS` | Poll interval in ms | `5000` |
| `SERVER_TIMEOUT_MINUTES` | Minutes before light sleep | `10` |
| `WAKEUP_GPIO_PIN` | GPIO pin to wake from sleep | `9` (BOOT button) |
| `WAKEUP_GPIO_ACTIVE_LOW` | Wakeup pin is active low | enabled |

### Build

```bash
idf.py build
```

This also generates `firmware/build/compile_commands.json` for IDE IntelliSense.

### Flash & Monitor

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with your device's serial port (`/dev/cu.usbserial-*` on macOS).
