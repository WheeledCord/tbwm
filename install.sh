#!/bin/bash
set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

info() { echo -e "${BLUE}[INFO]${NC} $1"; }
success() { echo -e "${GREEN}[OK]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

# Detect distro
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        DISTRO="$ID"
        DISTRO_LIKE="$ID_LIKE"
    elif [ -f /etc/arch-release ]; then
        DISTRO="arch"
    elif [ -f /etc/debian_version ]; then
        DISTRO="debian"
    elif [ -f /etc/fedora-release ]; then
        DISTRO="fedora"
    else
        DISTRO="unknown"
    fi
    info "Detected distro: $DISTRO"
}

# Check if running as root
check_not_root() {
    if [ "$EUID" -eq 0 ]; then
        error "Don't run this script as root. It will ask for sudo when needed."
    fi
}

# Install dependencies based on distro
install_deps() {
    info "Installing dependencies..."
    
    case "$DISTRO" in
        arch|endeavouros|manjaro|garuda|cachyos)
            # Arch-based
            sudo pacman -S --needed --noconfirm \
                wlroots wayland wayland-protocols libinput libxkbcommon \
                pixman freetype2 pango cairo libxcb xcb-util-wm \
                xorg-xwayland meson ninja gcc pkgconf make git \
                fontconfig ttf-font || true
            ;;
        debian|ubuntu|pop|linuxmint|elementary)
            # Debian-based
            sudo apt-get update
            sudo apt-get install -y \
                libwlroots-dev libwayland-dev wayland-protocols \
                libinput-dev libxkbcommon-dev libpixman-1-dev \
                libfreetype-dev libpango1.0-dev libcairo2-dev \
                libxcb1-dev libxcb-icccm4-dev xwayland \
                meson ninja-build gcc pkg-config make git \
                fontconfig || true
            ;;
        fedora|rhel|centos|rocky|almalinux)
            # Fedora/RHEL-based
            sudo dnf install -y \
                wlroots-devel wayland-devel wayland-protocols-devel \
                libinput-devel libxkbcommon-devel pixman-devel \
                freetype-devel pango-devel cairo-devel \
                libxcb-devel xcb-util-wm-devel xorg-x11-server-Xwayland \
                meson ninja-build gcc pkg-config make git \
                fontconfig || true
            ;;
        opensuse*|suse*)
            # openSUSE
            sudo zypper install -y \
                wlroots-devel wayland-devel wayland-protocols-devel \
                libinput-devel libxkbcommon-devel libpixman-1-0-devel \
                freetype2-devel pango-devel cairo-devel \
                libxcb-devel xwayland meson ninja gcc pkg-config make git \
                fontconfig || true
            ;;
        void)
            # Void Linux
            sudo xbps-install -Sy \
                wlroots-devel wayland-devel wayland-protocols \
                libinput-devel libxkbcommon-devel pixman-devel \
                freetype-devel pango-devel cairo-devel \
                libxcb-devel xcb-util-wm-devel xorg-server-xwayland \
                meson ninja gcc pkg-config make git fontconfig || true
            ;;
        gentoo)
            warn "Gentoo detected. Please ensure you have the following USE flags enabled:"
            warn "  dev-libs/wlroots gui-wm/dwl"
            warn "Run: sudo emerge --ask wlroots wayland freetype pango xwayland"
            ;;
        nixos)
            warn "NixOS detected. Dependencies should be in your configuration.nix"
            warn "Or use: nix-shell -p wlroots wayland freetype pango"
            ;;
        *)
            warn "Unknown distro: $DISTRO"
            warn "Please install these dependencies manually:"
            echo "  - wlroots >= 0.19"
            echo "  - wayland, wayland-protocols"
            echo "  - libinput, libxkbcommon"
            echo "  - freetype2, pango, cairo, pixman"
            echo "  - libxcb, xcb-util-wm"
            echo "  - xwayland (optional, for X11 apps)"
            echo ""
            read -p "Continue anyway? [y/N] " -n 1 -r
            echo
            if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                exit 1
            fi
            ;;
    esac
    success "Dependencies installed"
}

# Check wlroots version
check_wlroots() {
    info "Checking wlroots version..."
    
    if ! pkg-config --exists wlroots-0.19 2>/dev/null; then
        if pkg-config --exists wlroots 2>/dev/null; then
            WLROOTS_VER=$(pkg-config --modversion wlroots 2>/dev/null || echo "unknown")
            warn "Found wlroots $WLROOTS_VER but need wlroots 0.19+"
        else
            warn "wlroots not found"
        fi
        
        echo ""
        echo "TurboWM requires wlroots 0.19 or newer."
        echo ""
        echo "Options:"
        echo "  1) Build wlroots 0.19 from source (recommended)"
        echo "  2) Skip and try to build anyway"
        echo "  3) Exit"
        echo ""
        read -p "Choice [1/2/3]: " -n 1 -r
        echo
        
        case $REPLY in
            1)
                build_wlroots
                ;;
            2)
                warn "Continuing without wlroots 0.19 - build may fail"
                ;;
            *)
                exit 1
                ;;
        esac
    else
        WLROOTS_VER=$(pkg-config --modversion wlroots-0.19)
        success "Found wlroots $WLROOTS_VER"
    fi
}

# Build wlroots from source
build_wlroots() {
    info "Building wlroots 0.19 from source..."
    
    # Install meson build deps
    case "$DISTRO" in
        arch|endeavouros|manjaro|garuda|cachyos)
            sudo pacman -S --needed --noconfirm \
                meson ninja hwdata libdisplay-info libliftoff seatd || true
            ;;
        debian|ubuntu|pop|linuxmint|elementary)
            sudo apt-get install -y \
                meson ninja-build libdrm-dev libgbm-dev libseat-dev \
                libdisplay-info-dev libliftoff-dev hwdata || true
            ;;
        fedora|rhel|centos|rocky|almalinux)
            sudo dnf install -y \
                meson ninja-build libdrm-devel mesa-libgbm-devel \
                libseat-devel libdisplay-info-devel libliftoff-devel hwdata || true
            ;;
    esac
    
    WLROOTS_BUILD_DIR="/tmp/wlroots-build-$$"
    mkdir -p "$WLROOTS_BUILD_DIR"
    cd "$WLROOTS_BUILD_DIR"
    
    info "Cloning wlroots..."
    git clone --depth 1 --branch 0.19.0 https://gitlab.freedesktop.org/wlroots/wlroots.git
    cd wlroots
    
    info "Configuring..."
    meson setup build --prefix=/usr/local -Dexamples=false
    
    info "Building..."
    ninja -C build
    
    info "Installing (requires sudo)..."
    sudo ninja -C build install
    
    # Update library cache
    sudo ldconfig
    
    # Cleanup
    cd /
    rm -rf "$WLROOTS_BUILD_DIR"
    
    # Check if it worked
    export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:/usr/local/lib64/pkgconfig:$PKG_CONFIG_PATH"
    if pkg-config --exists wlroots-0.19 2>/dev/null; then
        success "wlroots 0.19 installed to /usr/local"
    else
        error "wlroots installation failed"
    fi
}

# Build TurboWM
build_tbwm() {
    info "Building TurboWM..."
    
    # Make sure we can find locally-installed wlroots
    export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:/usr/local/lib64/pkgconfig:$PKG_CONFIG_PATH"
    export LD_LIBRARY_PATH="/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH"
    
    TMPDIR="$HOME" make clean 2>/dev/null || true
    TMPDIR="$HOME" make -j$(nproc)
    
    success "TurboWM built successfully"
}

# Install TurboWM
install_tbwm() {
    info "Installing TurboWM..."
    
    # Binary
    sudo cp tbwm /usr/local/bin/
    sudo chmod 755 /usr/local/bin/tbwm
    success "Installed /usr/local/bin/tbwm"
    
    # Font
    sudo mkdir -p /usr/share/fonts/tbwm
    sudo cp PxPlus_IBM_VGA_8x16.ttf /usr/share/fonts/tbwm/
    sudo fc-cache -f /usr/share/fonts/tbwm 2>/dev/null || true
    success "Installed font"
    
    # Session file for display managers
    sudo mkdir -p /usr/share/wayland-sessions
    sudo tee /usr/share/wayland-sessions/tbwm.desktop > /dev/null << 'EOF'
[Desktop Entry]
Name=TurboWM
Comment=A Wayland compositor with s7 Scheme configuration
Exec=/usr/local/bin/tbwm
Type=Application
EOF
    success "Installed session file"
    
    # Create default config if it doesn't exist
    if [ ! -f "$HOME/.config/tbwm/config.scm" ]; then
        mkdir -p "$HOME/.config/tbwm"
        if [ -f "docs/example-config.scm" ]; then
            cp docs/example-config.scm "$HOME/.config/tbwm/config.scm"
            success "Created default config at ~/.config/tbwm/config.scm"
        fi
    fi
    
    # Create wrapper script that sets LD_LIBRARY_PATH (for custom wlroots)
    sudo tee /usr/local/bin/tbwm-wrapper > /dev/null << 'EOF'
#!/bin/bash
export LD_LIBRARY_PATH="/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH"
exec /usr/local/bin/tbwm "$@"
EOF
    sudo chmod 755 /usr/local/bin/tbwm-wrapper
    
    # Update session to use wrapper
    sudo sed -i 's|Exec=/usr/local/bin/tbwm|Exec=/usr/local/bin/tbwm-wrapper|' \
        /usr/share/wayland-sessions/tbwm.desktop
}

# Main
main() {
    echo ""
    echo "╔════════════════════════════════════════╗"
    echo "║     TurboWM Installer                  ║"
    echo "╚════════════════════════════════════════╝"
    echo ""alternitivealternitive
    
    check_not_root
    detect_distro
    
    # Ask what to do
    echo ""
    echo "Options:"
    echo "  1) Full install (deps + build + install)"
    echo "  2) Build only (assumes deps installed)"
    echo "  3) Install only (assumes already built)"
    echo ""
    read -p "Choice [1/2/3]: " -n 1 -r
    echo
    
    case $REPLY in
        1)
            install_deps
            check_wlroots
            build_tbwm
            install_tbwm
            ;;
        2)
            check_wlroots
            build_tbwm
            ;;
        3)
            install_tbwm
            ;;
        *)
            error "Invalid choice"
            ;;
    esac
    
    echo ""
    echo "╔════════════════════════════════════════╗"
    echo "║     Installation Complete!             ║"
    echo "╚════════════════════════════════════════╝"
    echo ""
    echo "To use TurboWM:"
    echo "  • Log out and select 'TurboWM' from your display manager"
    echo "  • Or run: tbwm-wrapper (from a TTY)"
    echo ""
    echo "Config file: ~/.config/tbwm/config.scm"
    echo ""
}

main "$@"
