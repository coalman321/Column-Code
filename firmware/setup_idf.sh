#!/usr/bin/env bash
set -euo pipefail

IDF_VERSION="${IDF_VERSION:-v5.4.1}"
IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf}"

# ── colours ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()    { echo -e "${GREEN}[setup]${NC} $*"; }
warn()    { echo -e "${YELLOW}[setup]${NC} $*"; }
error()   { echo -e "${RED}[setup]${NC} $*" >&2; exit 1; }

# ── OS detection ─────────────────────────────────────────────────────────────
case "$(uname -s)" in
    Linux*)  OS=linux ;;
    Darwin*) OS=macos ;;
    *)       error "Unsupported OS: $(uname -s)" ;;
esac

# ── system dependencies ──────────────────────────────────────────────────────
install_deps_linux() {
    info "Installing system dependencies..."
    sudo apt-get update -qq
    sudo apt-get install -y \
        git wget flex bison gperf \
        python3 python3-pip python3-venv \
        cmake ninja-build ccache \
        libffi-dev libssl-dev dfu-util \
        libusb-1.0-0
}

install_deps_macos() {
    if ! command -v brew &>/dev/null; then
        error "Homebrew not found. Install it from https://brew.sh first."
    fi
    info "Installing system dependencies via Homebrew..."
    brew install cmake ninja dfu-util python3
}

check_cmd() {
    command -v "$1" &>/dev/null
}

missing_deps=()
for cmd in git python3 cmake ninja; do
    check_cmd "$cmd" || missing_deps+=("$cmd")
done

if [[ ${#missing_deps[@]} -gt 0 ]]; then
    warn "Missing: ${missing_deps[*]}"
    if [[ "$OS" == "linux" ]]; then
        install_deps_linux
    else
        install_deps_macos
    fi
fi

# ── clone or update ESP-IDF ───────────────────────────────────────────────────
if [[ -d "$IDF_PATH/.git" ]]; then
    info "ESP-IDF found at $IDF_PATH"
    current=$(git -C "$IDF_PATH" describe --tags 2>/dev/null || echo "unknown")
    if [[ "$current" == "$IDF_VERSION" ]]; then
        info "Already at $IDF_VERSION, skipping clone."
    else
        warn "Current version: $current. Fetching $IDF_VERSION..."
        git -C "$IDF_PATH" fetch --tags
        git -C "$IDF_PATH" checkout "$IDF_VERSION"
        git -C "$IDF_PATH" submodule update --init --recursive
    fi
else
    info "Cloning ESP-IDF $IDF_VERSION to $IDF_PATH..."
    mkdir -p "$(dirname "$IDF_PATH")"
    git clone --branch "$IDF_VERSION" --depth 1 \
        https://github.com/espressif/esp-idf.git "$IDF_PATH"
    git -C "$IDF_PATH" submodule update --init --recursive
fi

# ── run ESP-IDF installer ─────────────────────────────────────────────────────
info "Running ESP-IDF install script (target: esp32c6)..."
"$IDF_PATH/install.sh" esp32c6

# ── shell profile setup ───────────────────────────────────────────────────────
EXPORT_LINE=". $IDF_PATH/export.sh"
SHELL_RC=""

if [[ -n "${ZSH_VERSION:-}" ]] || [[ "$SHELL" == */zsh ]]; then
    SHELL_RC="$HOME/.zshrc"
elif [[ -n "${BASH_VERSION:-}" ]] || [[ "$SHELL" == */bash ]]; then
    SHELL_RC="$HOME/.bashrc"
fi

if [[ -n "$SHELL_RC" ]]; then
    if grep -qF "$EXPORT_LINE" "$SHELL_RC" 2>/dev/null; then
        info "export.sh already sourced in $SHELL_RC"
    else
        warn "Adding ESP-IDF export to $SHELL_RC"
        printf '\n# ESP-IDF\n%s\n' "$EXPORT_LINE" >> "$SHELL_RC"
    fi
fi

# ── done ─────────────────────────────────────────────────────────────────────
echo ""
info "ESP-IDF $IDF_VERSION setup complete."
echo ""
echo "  To activate in your current shell, run:"
echo "    . $IDF_PATH/export.sh"
echo ""
echo "  Then build the firmware:"
echo "    cd $(dirname "$0")"
echo "    idf.py set-target esp32c6"
echo "    idf.py build"
echo ""
