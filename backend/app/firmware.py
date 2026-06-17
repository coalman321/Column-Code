import struct
from pathlib import Path

from fastapi import APIRouter, HTTPException
from fastapi.responses import FileResponse

router = APIRouter(prefix="/firmware")

FIRMWARE_BIN = Path(__file__).parent.parent / "firmware" / "app.bin"
FIRMWARE_VERSION = "1.0.0"  # Must match firmware/main/version.h

_APP_DESC_MAGIC = struct.pack("<I", 0xABCD5432)


def _extract_elf_sha256(data: bytes) -> str:
    """
    Locate esp_app_desc_t in the binary by its magic word and extract
    app_elf_sha256, which IDF embeds at build time from the ELF file.

    esp_app_desc_t layout (IDF 5.x):
      magic_word      4 B
      secure_version  4 B
      reserv1[2]      8 B
      version[32]    32 B
      project_name   32 B
      time[16]       16 B
      date[16]       16 B
      idf_ver[32]    32 B
      app_elf_sha256 32 B  ← 144 bytes from struct start
    """
    offset = data.find(_APP_DESC_MAGIC)
    if offset == -1:
        raise ValueError("app description magic not found — is this a valid IDF binary?")
    sha_offset = offset + 144
    return data[sha_offset : sha_offset + 32].hex()


def _load_binary() -> bytes:
    if not FIRMWARE_BIN.exists():
        raise HTTPException(status_code=404, detail="No firmware binary uploaded yet")
    return FIRMWARE_BIN.read_bytes()


@router.get("/hash")
def firmware_hash():
    """Return the ELF SHA256 embedded in the firmware binary."""
    data = _load_binary()
    try:
        sha256 = _extract_elf_sha256(data)
    except ValueError as exc:
        raise HTTPException(status_code=422, detail=str(exc))
    return {"sha256": sha256}


@router.get("/bin")
def firmware_bin():
    """Serve the latest firmware binary for OTA download."""
    if not FIRMWARE_BIN.exists():
        raise HTTPException(status_code=404, detail="No firmware binary uploaded yet")
    return FileResponse(
        FIRMWARE_BIN,
        media_type="application/octet-stream",
        filename="app.bin",
    )


@router.get("/version")
def firmware_version():
    """Get the current firmware version."""
    return {"version": FIRMWARE_VERSION}
