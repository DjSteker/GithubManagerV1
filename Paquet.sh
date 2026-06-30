#!/usr/bin/env bash
set -eu

# scripts/install-deps.sh
# Intenta instalar dependencias requeridas para compilar:
# pkg-config, build tools, gtk4 dev, tinyxml2 dev, openssl dev, git
#
# Soporta (intentos): debian/ubuntu/kali/zorin, arch/manjaro, fedora/alma/rocky, opensuse, redhat (dnf),
# slackware/tinycore (instrucciones manuales)

if [ "$EUID" -ne 0 ]; then
  echo "Este script intenta usar sudo para instalar paquetes. Se te pedirá la contraseña si hace falta."
fi

install_cmd() {
  echo "Ejecutando: $*"
  if ! "$@"; then
    echo "Error ejecutando: $*"
    return 1
  fi
}

if [ -f /etc/os-release ]; then
  . /etc/os-release
  ID_LC=$(echo "$ID" | tr '[:upper:]' '[:lower:]')
else
  ID_LC="unknown"
fi

echo "Detectado: $NAME ($ID_LC)"
case "$ID_LC" in
  ubuntu|debian|linuxmint|pop|zorin|kali)
    echo "Usando apt (Debian/Ubuntu/Zorin/Kali)..."
    sudo apt update
    install_cmd sudo apt install -y build-essential pkg-config git
    install_cmd sudo apt install -y libgtk-4-dev libtinyxml2-dev libssl-dev
    ;;

  fedora|fedora_linux|centos|rhel)
    echo "Usando dnf (Fedora/RHEL/CentOS/Alma/Rocky)..."
    # En RHEL/CentOS puede que necesites habilitar repos extras o EPEL; el usuario debe revisar.
    install_cmd sudo dnf install -y @development-tools pkg-config git
    install_cmd sudo dnf install -y gtk4-devel tinyxml2-devel openssl-devel
    ;;

  almalinux|rocky)
    echo "AlmaLinux/Rocky: usando dnf (misma familia RHEL)"
    install_cmd sudo dnf install -y @development-tools pkg-config git
    install_cmd sudo dnf install -y gtk4-devel tinyxml2-devel openssl-devel
    ;;

  opensuse*|sles)
    echo "Usando zypper (openSUSE/SLES)..."
    install_cmd sudo zypper refresh
    install_cmd sudo zypper install -y -t pattern devel_C_C++
    install_cmd sudo zypper install -y pkg-config git gtk4-devel libopenssl-devel libtinyxml2-devel
    ;;

  arch|manjaro)
    echo "Usando pacman (Arch/Manjaro)..."
    install_cmd sudo pacman -Syu --noconfirm
    install_cmd sudo pacman -S --noconfirm base-devel pkgconf git
    install_cmd sudo pacman -S --noconfirm gtk4 tinyxml2 openssl
    ;;

  zorin)
    echo "Zorin OS (basado en Ubuntu) - usando apt"
    sudo apt update
    install_cmd sudo apt install -y build-essential pkg-config git libgtk-4-dev libtinyxml2-dev libssl-dev
    ;;

  slackware)
    echo "Slackware detectado. No automatizo la instalación de dependencias en Slackware."
    echo "Opciones:"
    echo " - Usar slackpkg o installpkg con los paquetes adecuados."
    echo " - Usar sbopkg / SlackBuilds para construir gtk4, tinyxml2 y openssl dev packages."
    echo "Consulta: https://www.slackware.com/ o SlackBuilds.org"
    ;;

  tinycore)
    echo "Tiny Core Linux detectado. Instalación manual requerida."
    echo "Intenta instalar paquetes dev con tce-load, o compilar dependencias desde fuente."
    echo "Ejemplo: tce-load -wi glibc-dev gcc make pkg-config"
    echo "GTK4/tinyxml2 y openssl podrían no estar disponibles en repos oficiales y requerir compilación."
    ;;

  *)
    echo "Distribución no detectada o no soportada automáticamente: $ID_LC"
    echo "Instala manualmente las dependencias:"
    echo "  - pkg-config"
    echo "  - compilador C++ (g++), herramientas de build (make, gcc/make) o group de desarrollo"
    echo "  - libgtk-4 (devel), libtinyxml2 (devel), libssl/openssl (devel)"
    echo "  - git"
    ;;
esac

echo ""
echo "Instalación finalizada (o instrucciones mostradas)."
echo "Recomendado: ejecutar luego: pkg-config --cflags gtk4 tinyxml2 openssl"
echo "Si pkg-config devuelve valores, ya puedes compilar con:"
echo "  make -f makelist.txt"
