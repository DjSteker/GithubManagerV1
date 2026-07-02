/*
 * GestorGit.cpp
 *
 * Created on: 30 jun 2026
 *   Author: DjSteker
 */

#include "GestorGit.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <openssl/crypto.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers internos
// ---------------------------------------------------------------------------

static std::string escaparParaComillasDobles(const std::string &texto) {
  std::string resultado;
  resultado.reserve(texto.size() * 2);
  for (char caracter : texto) {
    if (caracter == '"' || caracter == '\\' || caracter == '$' || caracter == '`' || caracter == '\'' || caracter == '!') {
      resultado += '\\';
    }
    resultado += caracter;
  }
  return resultado;
}

// Elimina saltos de línea finales de una cadena.
static std::string trimLineas(const std::string &s) {
  std::string r = s;
  while (!r.empty() && (r.back() == '\n' || r.back() == '\r')) {
    r.pop_back();
  }
  return r;
}

// ---------------------------------------------------------------------------
// RAII para archivo temporal askpass
// ---------------------------------------------------------------------------

struct TempFileHandle {
  int fd;
  std::string path;
  bool keep;

  TempFileHandle(int fd_, std::string path_) : fd(fd_), path(std::move(path_)), keep(false) {}

  ~TempFileHandle() {
    if (fd != -1) {
      close(fd);
      fd = -1;
    }
    if (!keep && !path.empty()) {
      std::error_code ec;
      fs::remove(path, ec);
    }
  }
};

// ---------------------------------------------------------------------------
// Métodos públicos de validación
// ---------------------------------------------------------------------------

bool GestorGit::validarUrlRepositorio(const std::string &url) {
  if (url.empty()) {
    return false;
  }

  bool esHttps = (url.find("https://") == 0 || url.find("http://") == 0);
  bool esSsh = (url.find("git@") == 0);

  if (!esHttps && !esSsh) {
    return true;  // URL relativa u otro formato aceptado
  }

  if (esHttps) {
    size_t finProtocolo = url.find("://") + 3;
    size_t primeraBarra = url.find('/', finProtocolo);
    size_t finAuthority = (primeraBarra == std::string::npos) ? url.length() : primeraBarra;
    std::string authority = url.substr(finProtocolo, finAuthority - finProtocolo);

    // Rechazar credenciales embebidas (usuario:contraseña@host)
    if (authority.find('@') != std::string::npos) {
      return false;
    }
  }

  return true;
}

std::string GestorGit::filtrarLogSensitive(const std::string &log) {
  std::string resultado = log;

  static const std::vector<std::pair<std::string, std::string>> filtros = {
    { "password=", "[REDACTED]" },
    { "Password:", "[REDACTED]" },
    { "authentication failed", "[REDACTED AUTH]" },
    { "could not read Username", "[REDACTED]" },
    { "could not read Password", "[REDACTED]" },
  };

  for (const auto &par : filtros) {
    size_t pos = 0;
    while ((pos = resultado.find(par.first, pos)) != std::string::npos) {
      resultado.replace(pos, par.first.length(), par.second);
      pos += par.second.length();
    }
  }

  return resultado;
}

// ---------------------------------------------------------------------------
// Métodos privados: askpass y ejecución de comandos
// ---------------------------------------------------------------------------

std::string GestorGit::crearScriptAskpass(const std::string &token) {
  if (token.empty()) {
    return "";
  }

  std::string tmpl = "/tmp/.ghmgr_askpass_XXXXXX";
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');

  int fd = mkstemp(buf.data());
  if (fd == -1) {
    return "";
  }

  TempFileHandle guard(fd, std::string(buf.data()));

  std::string contenido = "#!/bin/sh\n";
  contenido += "printf '%s' \"" + escaparParaComillasDobles(token) + "\"\n";

  bool ok = false;
  try {
    ssize_t escritos = ::write(fd, contenido.data(), contenido.size());
    if (escritos != static_cast<ssize_t>(contenido.size())) {
      throw std::system_error(errno, std::generic_category(), "write failed");
    }
    if (fsync(fd) == -1) {
      throw std::system_error(errno, std::generic_category(), "fsync failed");
    }
    if (fchmod(fd, S_IRWXU) == -1) {
      throw std::system_error(errno, std::generic_category(), "fchmod failed");
    }
    guard.keep = true;
    if (guard.fd != -1) {
      close(guard.fd);
      guard.fd = -1;
    }
    ok = true;
  } catch (...) {
    ok = false;
  }

  if (!ok) {
    return "";
  }
  return guard.path;
}

void GestorGit::eliminarScriptAskpass(const std::string &rutaScript) {
  if (rutaScript.empty()) {
    return;
  }

  std::error_code ec;
  if (!fs::exists(rutaScript)) {
    return;
  }

  try {
    uintmax_t tamano = 0;
    try {
      tamano = fs::file_size(rutaScript);
    } catch (...) {
      tamano = 0;
    }

    if (tamano > 0) {
      int fd = open(rutaScript.c_str(), O_WRONLY);
      if (fd != -1) {
        const size_t bloque = 4096;
        std::vector<char> ceros(bloque, 0);
        uintmax_t restante = tamano;
        while (restante > 0) {
          size_t aEscribir = (restante > bloque) ? bloque : static_cast<size_t>(restante);
          ssize_t w = ::write(fd, ceros.data(), aEscribir);
          if (w == -1) {
            break;
          }
          restante -= static_cast<uintmax_t>(w);
        }
        fsync(fd);
        close(fd);
      }
    }

    fs::remove(rutaScript, ec);
  } catch (...) {
    // Limpieza de mejor esfuerzo; ignorar errores.
  }
}

std::string GestorGit::ejecutarComandoGit(const std::string &comando, const std::string &directorioTrabajo, const std::string &token, int *codigoSalida) {
  std::string rutaAskpass;

  // Forzar salida en inglés independientemente del idioma del sistema.
  // LC_ALL=C y LANG=C son el enfoque más robusto; LANGUAGE= vacía sobreescribe
  // cualquier LANGUAGE del entorno de usuario.  GIT_TERMINAL_PROMPT=0 evita
  // que git se quede esperando entrada interactiva.
  std::string prefijoEntorno = "LC_ALL=C LANG=C LANGUAGE= GIT_TERMINAL_PROMPT=0 ";

  if (!token.empty()) {
    rutaAskpass = crearScriptAskpass(token);
    if (!rutaAskpass.empty()) {
      prefijoEntorno += "GIT_ASKPASS=\"" + rutaAskpass + "\" ";
    }
  }

  std::string comandoCompleto;
  if (!directorioTrabajo.empty()) {
    comandoCompleto = prefijoEntorno + "git -C \"" + escaparParaComillasDobles(directorioTrabajo) + "\" " + comando + " 2>&1";
  } else {
    comandoCompleto = prefijoEntorno + "git " + comando + " 2>&1";
  }

  std::string resultado;
  char buffer[512];

  FILE *tuberia = popen(comandoCompleto.c_str(), "r");
  if (!tuberia) {
    if (codigoSalida) {
      *codigoSalida = -1;
    }
    if (!rutaAskpass.empty()) {
      eliminarScriptAskpass(rutaAskpass);
    }
    return "No se pudo ejecutar el comando git";
  }

  while (fgets(buffer, sizeof(buffer), tuberia) != nullptr) {
    resultado += buffer;
  }

  int estado = pclose(tuberia);
  if (codigoSalida) {
    if (estado == -1) {
      *codigoSalida = -1;
    } else if (WIFEXITED(estado)) {
      *codigoSalida = WEXITSTATUS(estado);
    } else {
      *codigoSalida = -1;
    }
  }

  if (!rutaAskpass.empty()) {
    eliminarScriptAskpass(rutaAskpass);
  }
  return resultado;
}

// ---------------------------------------------------------------------------
// Operaciones públicas
// ---------------------------------------------------------------------------

ResultadoOperacionGit GestorGit::clonarRepositorio(const std::string &urlRepositorio, const std::string &directorioDestino, const std::string &token) {
  ResultadoOperacionGit resultado{ false, "", "" };

  if (urlRepositorio.empty() || directorioDestino.empty()) {
    resultado.mensaje = "URL del repositorio o directorio destino vacíos";
    return resultado;
  }

  if (!validarUrlRepositorio(urlRepositorio)) {
    resultado.mensaje = "URL inválida: no se permiten credenciales embebidas";
    return resultado;
  }

  // Si el destino existe y no está vacío git clone fallará; informar antes.
  if (fs::exists(directorioDestino)) {
    bool vacio = true;
    for (const auto &_ : fs::directory_iterator(directorioDestino)) {
      (void)_;
      vacio = false;
      break;
    }
    if (!vacio) {
      resultado.mensaje = "El directorio destino existe y no está vacío; no se puede clonar allí";
      return resultado;
    }
  }

  std::string comando = "clone \"" + escaparParaComillasDobles(urlRepositorio) + "\" \"" + escaparParaComillasDobles(directorioDestino) + "\"";

  int codigoSalida = 0;
  std::string salida = filtrarLogSensitive(ejecutarComandoGit(comando, "", token, &codigoSalida));

  resultado.salidaCompleta = salida;
  resultado.exito = (codigoSalida == 0);
  resultado.mensaje = resultado.exito ? "Repositorio clonado correctamente" : "Error al clonar el repositorio";
  return resultado;
}

ResultadoOperacionGit GestorGit::bajarCambios(const std::string &directorio, const std::string &rama, const std::string &token) {
  ResultadoOperacionGit resultado{ false, "", "" };

  if (directorio.empty()) {
    resultado.mensaje = "Directorio vacío";
    return resultado;
  }

  if (!fs::exists(fs::path(directorio) / ".git")) {
    resultado.mensaje = "El directorio no es un repositorio git (.git no encontrado)";
    return resultado;
  }

  std::string ramaEfectiva = rama.empty() ? "HEAD" : rama;
  std::string comando = "pull origin " + ramaEfectiva;

  int codigoSalida = 0;
  std::string salida = filtrarLogSensitive(
    ejecutarComandoGit(comando, directorio, token, &codigoSalida));

  resultado.salidaCompleta = salida;
  resultado.exito = (codigoSalida == 0);
  resultado.mensaje = resultado.exito ? "Cambios descargados correctamente"
                                      : "Error al descargar cambios";
  return resultado;
}

ResultadoOperacionGit GestorGit::subirCambios(const std::string &directorio, const std::string &rama, const std::string &mensajeCommit,
                                              const std::string &token, const std::string &urlOpcional) {
  ResultadoOperacionGit resultado{ false, "", "" };
  std::ostringstream log;
  int ec = 0;

  if (directorio.empty()) {
    resultado.mensaje = "Directorio vacío";
    return resultado;
  }

  // ------------------------------------------------------------------
  // 1. LOCALIZAR O INICIALIZAR EL REPOSITORIO
  // ------------------------------------------------------------------
  std::string repoRoot;
  bool esRepoNuevo = false;

  // Buscar .git en el directorio dado y en sus padres.
  {
    fs::path current = fs::path(directorio);
    bool found = false;
    while (true) {
      if (fs::exists(current / ".git")) {
        repoRoot = current.string();
        found = true;
        break;
      }
      if (!current.has_parent_path() || current == current.parent_path()) {
        break;
      }
      current = current.parent_path();
    }

    if (!found) {
      // No existe .git → inicializar en el directorio indicado.
      esRepoNuevo = true;
      repoRoot = directorio;
    }
  }

  if (esRepoNuevo) {
    // Crear el directorio si no existe.
    {
      std::error_code ecDir;
      fs::create_directories(repoRoot, ecDir);
    }

    std::string salidaInit = ejecutarComandoGit("init", repoRoot, "", &ec);
    log << salidaInit;
    if (ec != 0) {
      resultado.mensaje = "Error al inicializar el repositorio git";
      resultado.salidaCompleta = log.str();
      return resultado;
    }

    if (!urlOpcional.empty()) {
      if (!validarUrlRepositorio(urlOpcional)) {
        resultado.mensaje = "URL remota inválida (no se permiten credenciales embebidas)";
        resultado.salidaCompleta = log.str();
        return resultado;
      }
      std::string cmdRemote = "remote add origin \"" + escaparParaComillasDobles(urlOpcional) + "\"";
      std::string salidaRemote = ejecutarComandoGit(cmdRemote, repoRoot, "", &ec);
      log << salidaRemote;
      if (ec != 0) {
        resultado.mensaje = "Error al añadir el remote origin";
        resultado.salidaCompleta = log.str();
        return resultado;
      }
      log << "✓ Remote origin configurado: " << urlOpcional << "\n";
    }
  }

  // ------------------------------------------------------------------
  // 2. ACTUALIZAR URL REMOTA SI CAMBIÓ
  // ------------------------------------------------------------------
  if (!urlOpcional.empty() && !esRepoNuevo) {
    if (!validarUrlRepositorio(urlOpcional)) {
      resultado.mensaje = "URL remota inválida (no se permiten credenciales embebidas)";
      resultado.salidaCompleta = log.str();
      return resultado;
    }
    // Comprobar URL actual
    std::string salidaRemoteV = ejecutarComandoGit("remote -v", repoRoot, "", &ec);
    if (salidaRemoteV.find(urlOpcional) == std::string::npos) {
      std::string cmdSetUrl = "remote set-url origin \"" + escaparParaComillasDobles(urlOpcional) + "\"";
      ejecutarComandoGit(cmdSetUrl, repoRoot, "", &ec);
      if (ec == 0) {
        log << "✓ URL remota actualizada a: " << urlOpcional << "\n";
      } else {
        // Si no existe el remote, añadirlo
        std::string cmdAddRemote = "remote add origin \"" + escaparParaComillasDobles(urlOpcional) + "\"";
        ejecutarComandoGit(cmdAddRemote, repoRoot, "", &ec);
        log << "✓ Remote origin añadido: " << urlOpcional << "\n";
      }
    }
  }

  // ------------------------------------------------------------------
  // 3. DETECTAR RAMA ACTUAL
  // ------------------------------------------------------------------
  std::string ramaEfectiva;
  {
    std::string salidaRama = ejecutarComandoGit("rev-parse --abbrev-ref HEAD",
                                                repoRoot, "", &ec);
    ramaEfectiva = trimLineas(salidaRama);

    // HEAD en repos recién inicializados aún no tiene commits.
    if (ramaEfectiva == "HEAD" || ramaEfectiva.empty()) {
      ramaEfectiva = rama.empty() ? "main" : rama;
    }
  }

  // Si el usuario especificó una rama distinta y existe localmente, usarla.
  if (!rama.empty() && rama != ramaEfectiva) {
    std::string cmdCheck = "show-ref --verify --quiet refs/heads/" + rama;
    ejecutarComandoGit(cmdCheck, repoRoot, "", &ec);
    if (ec == 0) {
      ramaEfectiva = rama;
    } else {
      log << "⚠ Rama '" << rama << "' no existe; usando '" << ramaEfectiva << "'\n";
    }
  }

  // ------------------------------------------------------------------
  // 4. AÑADIR TODOS LOS ARCHIVOS
  // ------------------------------------------------------------------
  log << "📦 git add -A ...\n";
  std::string salidaAdd = ejecutarComandoGit("add -A", repoRoot, "", &ec);
  log << salidaAdd;

  // ------------------------------------------------------------------
  // 5. VERIFICAR EL ESTADO (porcelain = independiente del idioma)
  // ------------------------------------------------------------------
  std::string statusPorcelain = ejecutarComandoGit(
    "status --porcelain --untracked-files=normal", repoRoot, "", &ec);

  log << "\n📋 Estado:\n";
  if (statusPorcelain.empty()) {
    log << "  (árbol limpio, sin cambios pendientes)\n";
  } else {
    log << statusPorcelain;
  }
  log << "\n";

  // ------------------------------------------------------------------
  // 6. COMMIT (solo si hay algo en el índice)
  // ------------------------------------------------------------------
  bool hayCommitsPendientes = false;

  if (!statusPorcelain.empty()) {
    std::string mensajeEfectivo = mensajeCommit.empty() ? "Actualización" : mensajeCommit;
    std::string cmdCommit = "commit -m \"" + escaparParaComillasDobles(mensajeEfectivo) + "\"";

    log << "💾 git commit ...\n";
    std::string salidaCommitRaw = ejecutarComandoGit(cmdCommit, repoRoot, "", &ec);
    std::string salidaCommit = filtrarLogSensitive(salidaCommitRaw);
    log << salidaCommit << "\n";

    if (ec != 0) {
      // Doble comprobación: aun así podría ser "nothing to commit"
      // (p. ej. si add no añadió nada nuevo).
      std::string lc = salidaCommit;
      std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower);
      bool esNadaQueComitear =
        lc.find("nothing to commit") != std::string::npos || lc.find("no changes added to commit") != std::string::npos || lc.find("nothing added to commit") != std::string::npos;

      if (!esNadaQueComitear) {
        resultado.mensaje = "Error al crear el commit";
        resultado.salidaCompleta = log.str();
        return resultado;
      }
      // Árbol limpio → continuar hacia push de posibles commits anteriores.
      log << "✓ Nada nuevo que commitear; se intentará push de commits anteriores\n";
    } else {
      hayCommitsPendientes = true;
    }
  } else {
    // Nada en el índice → puede haber commits sin push de sesiones anteriores.
    log << "✓ Sin cambios en el árbol; se intentará push de commits anteriores\n";
  }

  (void)hayCommitsPendientes;  // variable de diagnóstico, sin uso posterior

  // ------------------------------------------------------------------
  // 7. VERIFICAR SI HAY REMOTE CONFIGURADO
  // ------------------------------------------------------------------
  bool tieneRemote = false;
  {
    std::string salidaRemoteV = ejecutarComandoGit("remote -v", repoRoot, "", &ec);
    tieneRemote = (salidaRemoteV.find("origin") != std::string::npos);
  }

  if (!tieneRemote) {
    resultado.exito = true;
    resultado.mensaje = "Cambios commiteados localmente (no hay remote configurado)";
    resultado.salidaCompleta = log.str();
    return resultado;
  }

  // ------------------------------------------------------------------
  // 8. PUSH
  // ------------------------------------------------------------------
  log << "📤 git push -u origin " << ramaEfectiva << " ...\n";
  std::string cmdPush = "push -u origin " + ramaEfectiva;
  std::string salidaPush = filtrarLogSensitive(ejecutarComandoGit(cmdPush, repoRoot, token, &ec));
  log << salidaPush << "\n";

  // Tratar "Everything up-to-date" (ec==0) y también el caso de rama
  // remota inexistente (ec!=0, "does not match any").
  if (ec != 0) {
    std::string lc = salidaPush;
    std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower);

    if (lc.find("does not match any") != std::string::npos || lc.find("src refspec") != std::string::npos) {
      // Intentar crear la rama remota explícitamente.
      log << "⚠ Rama remota inexistente; intentando crearla...\n";
      std::string cmdPushForce = "push -u origin HEAD:" + ramaEfectiva;
      salidaPush = filtrarLogSensitive(
        ejecutarComandoGit(cmdPushForce, repoRoot, token, &ec));
      log << salidaPush << "\n";
    }
  }

  resultado.salidaCompleta = log.str();
  resultado.exito = (ec == 0);
  resultado.mensaje = resultado.exito ? "Cambios subidos correctamente a '" + ramaEfectiva + "'" : "Error al subir cambios a '" + ramaEfectiva + "' (ver log)";

  return resultado;
}

ResultadoOperacionGit GestorGit::obtenerEstado(const std::string &directorio) {
  ResultadoOperacionGit resultado{ false, "", "" };

  if (directorio.empty() || !fs::exists(fs::path(directorio) / ".git")) {
    resultado.mensaje = "No es un repositorio git válido (.git no encontrado)";
    return resultado;
  }

  int ec = 0;
  std::string salida = ejecutarComandoGit("status -s -b", directorio, "", &ec);

  resultado.salidaCompleta = salida;
  resultado.exito = (ec == 0);
  resultado.mensaje = resultado.exito ? "Estado obtenido" : "Error obteniendo el estado del repositorio";
  return resultado;
}
