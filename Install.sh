#!/usr/bin/env bash
set -euo pipefail

# scripts/install-deps.sh
# Instala dependencias para GithubManager_V1 y verifica con pkg-config
# Soporta: Debian/Ubuntu/Mint/Pop/Zorin/Kali/Parrot/MX/antiX, 
# Fedora/RHEL/Alma/Rocky/Nobara, Arch/Manjaro/Garuda/EndeavourOS/BlackArch,
# openSUSE, Alpine, Void, Gentoo, Slackware, TinyCore

# --- Control de sudo ---
SUDO=""
if [ "${EUID:-$(id -u)}" -ne 0 ]; then
  # Si no es root, verificamos si sudo existe
  if command -v sudo >/dev/null 2>&1; then
    # Si la terminal es interactiva, preguntamos al usuario
    if [ -t 0 ]; then
      read -r -p "Se detectó 'sudo' en el sistema. ¿Desea utilizarlo para la instalación? [s/N] " -n1 reply || reply="n"
      echo ""
      if [[ "$reply" =~ ^[YySs]$ ]]; then
        SUDO="sudo"
      else
        echo "⚠️ Continuando sin sudo (la instalación podría fallar por falta de privilegios)."
      fi
    else
      # Si no es interactiva (ej. un script/CI), usamos sudo por defecto al estar disponible
      SUDO="sudo"
    fi
  else
    echo "⚠️ 'sudo' no está instalado. Se intentará continuar sin él."
  fi
fi

install_cmd() {
  echo "→ $*"
  if [ -n "$SUDO" ]; then
    $SUDO "$@" || { echo "✗ Error fatal en: $*"; exit 1; }
  else
    "$@" || { echo "✗ Error fatal en: $*"; exit 1; }
  fi
}

# --- Detección distro ---
if [ -f /etc/os-release ]; then
  . /etc/os-release
  ID_LC="${ID,,}"
  ID_LIKE_LC="${ID_LIKE,,}"
else
  ID_LC="unknown"; ID_LIKE_LC=""
fi

echo "🔍 Detectado: ${PRETTY_NAME:-$ID_LC} (ID=$ID_LC LIKE=$ID_LIKE_LC)"
has_like() { [[ "$ID_LIKE_LC" == *"$1"* ]]; }

# --- Instalación por familia ---
case "$ID_LC" in
  alpine)
    echo "Alpine Linux - apk"
    install_cmd apk update
    install_cmd apk add build-base pkgconf git make gtk4.0-dev tinyxml2-dev openssl-dev
    ;;
  void)
    echo "Void Linux - xbps"
    install_cmd xbps-install -Sy base-devel pkgconf git make
    install_cmd xbps-install -Sy gtk4-devel tinyxml2-devel openssl-devel
    ;;
  gentoo|sabayon|funtoo)
    echo "Gentoo-based - emerge"
    install_cmd emerge --sync
    install_cmd emerge -q dev-util/pkgconfig dev-vcs/git sys-devel/make x11-libs/gtk:4 dev-libs/tinyxml2 dev-libs/openssl
    ;;
  arch|manjaro|garuda|endeavouros|blackarch|artix|steamos)
    echo "Arch-based ($ID_LC) - pacman"
    install_cmd pacman -Syu --noconfirm
    install_cmd pacman -S --noconfirm base-devel pkgconf git make gtk4 tinyxml2 openssl
    ;;
  fedora|nobara|rhel|centos|almalinux|rocky|cloudlinux|ol)
    echo "RedHat-based ($ID_LC) - dnf"
    install_cmd dnf install -y @development-tools pkgconf-pkg-config git make
    install_cmd dnf install -y gtk4-devel tinyxml2-devel openssl-devel
    ;;
  opensuse*|sles)
    echo "openSUSE - zypper"
    install_cmd zypper refresh
    install_cmd zypper install -y -t pattern devel_C_C++
    install_cmd zypper install -y pkg-config git make gtk4-devel libtinyxml2-devel libopenssl-devel
    ;;
  debian|ubuntu|linuxmint|pop|elementary|zorin|kali|parrot|mx|antix|devuan|tails|proxmox|deepin|raspbian)
    echo "Debian-based ($ID_LC) - apt"
    install_cmd apt update
    install_cmd apt install -y build-essential pkg-config git make
    install_cmd apt install -y libgtk-4-dev libtinyxml2-dev libssl-dev
    ;;
  slackware)
    echo "❌ Slackware requiere instalación manual con slackpkg/sbopkg"
    echo "Necesitas: pkg-config, git, make, gtk4, tinyxml2, openssl (devel)"
    exit 1
    ;;
  tinycore|slitaz)
    echo "❌ Distro ligera - instala manualmente: compiletc pkg-config git"
    exit 1
    ;;
  *)
    # Fallback por ID_LIKE
    if has_like debian; then
      install_cmd apt update
      install_cmd apt install -y build-essential pkg-config git make libgtk-4-dev libtinyxml2-dev libssl-dev
    elif has_like arch; then
      install_cmd pacman -Syu --noconfirm base-devel pkgconf git make gtk4 tinyxml2 openssl
    elif has_like fedora || has_like rhel; then
      install_cmd dnf install -y @development-tools pkgconf-pkg-config git make gtk4-devel tinyxml2-devel openssl-devel
    elif has_like suse; then
      install_cmd zypper install -y pkg-config git make gtk4-devel libtinyxml2-devel libopenssl-devel
    elif has_like alpine; then
      install_cmd apk add build-base pkgconf git make gtk4.0-dev tinyxml2-dev openssl-dev
    else
      echo "❌ Distribución no detectada: $ID_LC"
      exit 1
    fi
    ;;
esac

echo ""
echo "✅ Verificando dependencias con pkg-config..."

verify_deps() {
  command -v pkg-config >/dev/null 2>&1 || return 1
  local cflags libs
  cflags=$(pkg-config --cflags gtk4 tinyxml2 openssl 2>/dev/null) || return 1
  libs=$(pkg-config --libs gtk4 tinyxml2 openssl 2>/dev/null) || return 1
  [ -n "$cflags" ] && [ -n "$libs" ]
}

if verify_deps; then
  echo "🎉 Dependencias OK"
  echo "   CFLAGS: $(pkg-config --cflags gtk4 tinyxml2 openssl)"
  echo "   LIBS:   $(pkg-config --libs gtk4 tinyxml2 openssl)"
else
  echo "⛔ pkg-config no encuentra gtk4, tinyxml2 u openssl"
  echo "Revisa que instalaste los paquetes -dev/-devel correctos"
  exit 1
fi

# --- Prompt de compilación ---
# --- Detección de Makefile disponible ---
MAKEFILE_FOUND=""
MAKE_CMD=""

if [ -f "./Makefile" ]; then
  MAKEFILE_FOUND="Makefile"
  MAKE_CMD="make all"
elif [ -f "./makelist.txt" ]; then
  MAKEFILE_FOUND="makelist.txt"
  MAKE_CMD="make -f makelist.txt all"
fi

# --- Función: intentar compilar ---
try_compile() {
  local cmd="$1"
  local alt_cmd="$2"
  local alt_makefile="$3"
  
  echo ""
  echo "----------------------------------------"
  echo "📝 Makefile detectado: $MAKEFILE_FOUND"
  echo "➡ Intentando: $cmd"
  echo ""
  
  if eval "$cmd"; then
    echo ""
    echo "✨ ¡Compilación completada correctamente!"
    echo "   Ejecutable: ./GithubManager"
    return 0
  else
    echo ""
    echo "❌ Falló la compilación con: $cmd"
    echo ""
    
    # Verificar si existe la alternativa
    if [ -n "$alt_cmd" ] && [ -f "$alt_makefile" ]; then
      if [ -t 0 ]; then
        echo "⚠️ Existe un archivo alternativo: $alt_makefile"
        read -r -p "¿Intentar compilar con: $alt_cmd? [s/N] " -n1 reply_alt || reply_alt="n"
        echo ""
        if [[ "$reply_alt" =~ ^[YySs]$ ]]; then
          echo "➡ Reintentando con: $alt_cmd"
          echo ""
          if eval "$alt_cmd"; then
            echo ""
            echo "✨ ¡Compilación completada con método alternativo!"
            echo "   Ejecutable: ./GithubManager"
            return 0
          else
            echo ""
            echo "❌ También falló el método alternativo."
            echo "   Revisa errores de código fuente o dependencias."
            return 1
          fi
        else
          echo "⏭ Método alternativo saltado."
          echo "   Puedes intentarlo manualmente luego: $alt_cmd"
          return 1
        fi
      else
        # No interactivo: intentar automáticamente la alternativa
        echo "⚙️ Modo no interactivo - probando alternativa automáticamente..."
        if eval "$alt_cmd"; then
          echo ""
          echo "✨ ¡Compilación completada con método alternativo!"
          return 0
        else
          echo "❌ Ambos métodos fallaron."
          return 1
        fi
      fi
    else
      echo "📌 No hay archivo Makefile alternativo disponible."
      echo "   Opciones:"
      echo "   - Renombra tu archivo a 'Makefile' y usa: make all"
      echo "   - O spécifica el archivo: make -f <archivo> all"
      return 1
    fi
  fi
}

# --- Ejecución principal ---
if [ -z "$MAKEFILE_FOUND" ]; then
  echo ""
  echo "❌ No se encontró ningún Makefile ni makelist.txt en el directorio actual."
  echo "   Asegúrate de estar en el directorio correcto del proyecto."
  exit 1
fi

if [ -t 0 ]; then
  # Modo interactivo: preguntar
  echo ""
  echo "----------------------------------------"
  echo "📦 Makefile detectado: $MAKEFILE_FOUND"
  echo "   Comando: $MAKE_CMD"
  read -r -p "¿Compilar ahora? [s/N] " -n1 reply_make || reply_make="n"
  echo ""
  
  if [[ "$reply_make" =~ ^[YySs]$ ]]; then
    # Determinar comando alternativo
    if [ "$MAKEFILE_FOUND" = "Makefile" ] && [ -f "./makelist.txt" ]; then
      ALT_CMD="make -f makelist.txt all"
      ALT_MF="makelist.txt"
    elif [ "$MAKEFILE_FOUND" = "makelist.txt" ] && [ -f "./Makefile" ]; then
      ALT_CMD="make all"
      ALT_MF="Makefile"
    else
      ALT_CMD=""
      ALT_MF=""
    fi
    
    try_compile "$MAKE_CMD" "$ALT_CMD" "$ALT_MF"
    exit $?
  else
    echo "⏭ Compilación saltada."
    echo "   Compila luego con: $MAKE_CMD"
    echo "   Alternativa:        ${ALT_CMD:-<no disponible>}"
    exit 0
  fi
else
  # Modo no interactivo (CI/pipe): compilar directo
  echo "⚙️ Modo no interactivo detectado."
  
  if [ "$MAKEFILE_FOUND" = "Makefile" ] && [ -f "./makelist.txt" ]; then
    ALT_CMD="make -f makelist.txt all"
    ALT_MF="makelist.txt"
  elif [ "$MAKEFILE_FOUND" = "makelist.txt" ] && [ -f "./Makefile" ]; then
    ALT_CMD="make all"
    ALT_MF="Makefile"
  else
    ALT_CMD=""
    ALT_MF=""
  fi
  
  try_compile "$MAKE_CMD" "$ALT_CMD" "$ALT_MF"
  exit $?
fi