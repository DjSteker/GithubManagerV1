/*
 * GestorGit.cpp
 *
 *  Created on: 30 jun 2026
 *      Author: DjSteker
 */

#include "GestorGit.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <random>
#include <algorithm>
#include <openssl/crypto.h>  // OPENSSL_cleanse
#include <system_error>

namespace fs = std::filesystem;

// Helper para sanitizar texto antes de usarlo en scripts shell
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

// NUEVO: Validar que la URL no tenga credenciales embebidas
bool GestorGit::validarUrlRepositorio(const std::string &url) {
  if (url.empty()) return false;

  // Buscar protocolo https:// o git@
  bool isHttps = (url.find("https://") == 0 || url.find("http://") == 0);
  bool isSsh = (url.find("git@") == 0);

  if (!isHttps && !isSsh) {
    // Podría ser una URL relativa u otra forma válida, permitirlo
    return true;
  }

  if (isHttps) {
    // Encontrar dónde termina el host (después del primer '/')
    size_t protocolEnd = url.find("://") + 3;
    size_t firstSlashAfterProtocol = url.find('/', protocolEnd);
    size_t checkTo = (firstSlashAfterProtocol == std::string::npos) ? url.length() : firstSlashAfterProtocol;

    std::string authorityPart = url.substr(protocolEnd, checkTo - protocolEnd);

    // Si contiene '@', podría tener user:token@host
    size_t atPos = authorityPart.find('@');
    if (atPos != std::string::npos) {
      // Hay credenciales embebidas - potencial riesgo
      // Para máxima seguridad, rechazamos cualquier URL con @
      return false;
    }
  }

  // URLs SSH tipo git@github.com:user/repo son generalmente seguras
  if (isSsh) {
    return true;
  }

  return true;
}

// NUEVO: Filtrar información sensible de los logs
std::string GestorGit::filtrarLogSensitive(const std::string &log) {
  std::string resultado = log;

  // Patrones comunes que podrían contener información sensible
  std::vector<std::pair<std::string, std::string>> filtros = {
    { "password=", "[REDACTED]" },
    { "Password:", "[REDACTED]" },
    { "authentication failed", "[REDACTED AUTH ERROR]" },
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

// RAII helper para asegurar cierre de fd y borrado del path si es necesario
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

std::string GestorGit::crearScriptAskpass(const std::string &token) {
  if (token.empty()) return "";

  std::string tmpl = "/tmp/.ghmgr_askpass_XXXXXX";
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');

  int fd = mkstemp(buf.data());
  if (fd == -1) {
    return "";
  }

  TempFileHandle guard(fd, std::string(buf.data()));

  // Construir contenido del script sin exponer token en logs intermediarios
  std::string contenido = "#!/bin/sh\n";
  // Usar printf para evitar interpretaciones de escape y mantener token en memoria segura
  contenido += "printf '%s' \"" + escaparParaComillasDobles(token) + "\"\n";

  bool ok = false;
  try {
    ssize_t written = ::write(fd, contenido.data(), contenido.size());
    if (written != (ssize_t)contenido.size()) {
      throw std::system_error(errno, std::generic_category(), "write failed");
    }

    // Forzar a disco y asegurar permisos del propietario exclusivamente (700)
    if (fsync(fd) == -1) {
      throw std::system_error(errno, std::generic_category(), "fsync failed");
    }

    if (fchmod(fd, S_IRWXU) == -1) {
      throw std::system_error(errno, std::generic_category(), "fchmod failed");
    }

    // Cerrar el fd ahora (TempFileHandle cerrará en su destructor si seguimos), pero queremos
    // devolver la ruta como fichero ejecutable. Marcamos keep=true para evitar que el destructor
    // borre el archivo y cerramos el descriptor.
    guard.keep = true;
    // cerramos el fd manualmente y evitamos doble close
    if (guard.fd != -1) {
      close(guard.fd);
      guard.fd = -1;
    }

    ok = true;
  } catch (...) {
    // Guard destructor eliminará el archivo y cerrará fd
    ok = false;
  }

  if (!ok) return "";

  return guard.path;
}

void GestorGit::eliminarScriptAskpass(const std::string &rutaScript) {
  if (rutaScript.empty()) return;

  std::error_code ec;
  if (!fs::exists(rutaScript)) return;

  try {
    // Intentar determinar tamaño del archivo de forma segura
    uintmax_t fileSize = 0;
    try {
      fileSize = fs::file_size(rutaScript);
    } catch (...) {
      fileSize = 0;
    }

    if (fileSize > 0) {
      // Abrir para sobrescribir
      int fd = open(rutaScript.c_str(), O_WRONLY);
      if (fd != -1) {
        // Preparar buffer de ceros en trozos para no consumir demasiada RAM
        const size_t chunk = 4096;
        std::vector<char> zeroBuf(chunk, 0);
        uintmax_t remaining = fileSize;
        while (remaining > 0) {
          size_t toWrite = (remaining > chunk) ? chunk : static_cast<size_t>(remaining);
          ssize_t w = ::write(fd, zeroBuf.data(), toWrite);
          if (w == -1) break; // no podemos hacer mucho más
          remaining -= static_cast<uintmax_t>(w);
        }
        fsync(fd);
        close(fd);
      }
    }

    // Finalmente borrar el archivo
    fs::remove(rutaScript, ec);
  } catch (...) {
    // Ignorar cualquier excepción - la eliminación es un intento de limpieza
  }
}

std::string GestorGit::ejecutarComandoGit(const std::string &comando, const std::string &directorioTrabajo,
                                          const std::string &token, int *codigoSalida) {
  std::string rutaAskpass;
  // Forzar salida de git en inglés, independientemente del idioma del sistema
  // (LANG/LC_ALL del usuario). Esto es necesario porque el código detecta condiciones
  // como "nothing to commit" buscando cadenas en inglés; con git en español esas
  // cadenas nunca coincidirían y se reportaría un error falso.
  std::string prefijoEntorno = "LC_ALL=C LANGUAGE=en GIT_TERMINAL_PROMPT=0 ";

  if (!token.empty()) {
    rutaAskpass = crearScriptAskpass(token);
    if (!rutaAskpass.empty()) {
      prefijoEntorno += "GIT_ASKPASS=\"" + rutaAskpass + "\" ";
    } else {
      // Si no pudo crear askpass, advertir pero continuar sin token
    }
  }

  std::string comandoCompleto;
  if (!directorioTrabajo.empty()) {
    comandoCompleto = prefijoEntorno + "git -C \"" + directorioTrabajo + "\" " + comando + " 2>&1";
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
    // Asegurarse de eliminar askpass si fue creado
    if (!rutaAskpass.empty()) eliminarScriptAskpass(rutaAskpass);
    return "No se pudo ejecutar el comando git";
  }

  while (fgets(buffer, sizeof(buffer), tuberia) != nullptr) {
    resultado += buffer;
  }

  int status = pclose(tuberia);
  if (codigoSalida) {
    if (status == -1) {
      *codigoSalida = -1;
    } else if (WIFEXITED(status)) {
      *codigoSalida = WEXITSTATUS(status);
    } else {
      *codigoSalida = -1;
    }
  }

  // Limpiar el script askpass creado
  if (!rutaAskpass.empty()) eliminarScriptAskpass(rutaAskpass);
  return resultado;
}

ResultadoOperacionGit GestorGit::clonarRepositorio(const std::string &urlRepositorio,
                                                   const std::string &directorioDestino,
                                                   const std::string &token) {
  ResultadoOperacionGit resultado{ false, "", "" };

  if (urlRepositorio.empty() || directorioDestino.empty()) {
    resultado.mensaje = "URL del repositorio o directorio destino vacíos";
    return resultado;
  }

  if (!validarUrlRepositorio(urlRepositorio)) {
    resultado.mensaje = "URL inválida: no se permiten credenciales embebidas (usar https://github.com/user/repo.git)";
    return resultado;
  }

  if (fs::exists(directorioDestino)) {
    bool vacio = true;
    for (auto &p : fs::directory_iterator(directorioDestino)) {
      vacio = false;
      break;
    }
    if (!vacio) {
      resultado.mensaje = "El directorio destino existe y no está vacío; no se puede clonar allí.";
      resultado.exito = false;
      return resultado;
    }
  }

  std::string comando = "clone \"" + escaparParaComillasDobles(urlRepositorio) + "\" \"" + escaparParaComillasDobles(directorioDestino) + "\"";

  int codigoSalida = 0;
  std::string salidaRaw = ejecutarComandoGit(comando, "", token, &codigoSalida);

  std::string salidaFiltrada = filtrarLogSensitive(salidaRaw);

  resultado.salidaCompleta = salidaFiltrada;
  resultado.exito = (codigoSalida == 0);
  resultado.mensaje = resultado.exito ? "Repositorio clonado correctamente" : "Error al clonar el repositorio";
  return resultado;
}

ResultadoOperacionGit GestorGit::bajarCambios(const std::string &directorio,
                                              const std::string &rama,
                                              const std::string &token) {
  ResultadoOperacionGit resultado{ false, "", "" };

  if (directorio.empty()) {
    resultado.mensaje = "Directorio vacío";
    return resultado;
  }

  if (!fs::exists(fs::path(directorio) / ".git")) {
    resultado.mensaje = "El directorio no es un repositorio git";
    return resultado;
  }

  std::string ramaEfectiva = rama.empty() ? "HEAD" : rama;
  std::string comando = "pull origin " + ramaEfectiva;

  int codigoSalida = 0;
  std::string salidaRaw = ejecutarComandoGit(comando, directorio, token, &codigoSalida);
  std::string salidaFiltrada = filtrarLogSensitive(salidaRaw);

  resultado.salidaCompleta = salidaFiltrada;
  resultado.exito = (codigoSalida == 0);
  resultado.mensaje = resultado.exito ? "Cambios descargados correctamente" : "Error al descargar cambios";
  return resultado;
}

ResultadoOperacionGit GestorGit::subirCambios(const std::string &directorio, const std::string &rama,
                                              const std::string &mensajeCommit, const std::string &token,
                                              const std::string &urlOpcional) {
  ResultadoOperacionGit resultado{ false, "", "" };

  if (directorio.empty()) {
    resultado.mensaje = "Directorio vacío";
    return resultado;
  }

  if (!urlOpcional.empty() && !validarUrlRepositorio(urlOpcional)) {
    resultado.mensaje = "URL remota inválida: contiene credenciales embebidas";
    return resultado;
  }

  bool esRepoNuevo = !fs::exists(fs::path(directorio) / ".git");
  std::ostringstream salidaCompleta;
  int codigoSalida = 0;

  if (esRepoNuevo) {
    std::string salidaInit = ejecutarComandoGit("init", directorio, "", &codigoSalida);
    salidaCompleta << salidaInit;
    if (codigoSalida != 0) {
      resultado.mensaje = "Error inicializando el repositorio local";
      resultado.salidaCompleta = salidaCompleta.str();
      return resultado;
    }

    if (!urlOpcional.empty()) {
      std::string cmdRemote = "remote add origin \"" + escaparParaComillasDobles(urlOpcional) + "\"";
      std::string salidaRemote = ejecutarComandoGit(cmdRemote, directorio, "", &codigoSalida);
      salidaCompleta << salidaRemote;
      if (codigoSalida != 0) {
        resultado.mensaje = "Error añadiendo remote origin";
        resultado.salidaCompleta = salidaCompleta.str();
        return resultado;
      }
    }
  }

  std::string salidaAdd = ejecutarComandoGit("add -A", directorio, "", &codigoSalida);
  salidaCompleta << salidaAdd;

  std::string mensajeEfectivo = mensajeCommit.empty() ? "Actualización" : mensajeCommit;
  std::string comandoCommit = "commit -m \"" + escaparParaComillasDobles(mensajeEfectivo) + "\"";
  std::string salidaCommitRaw = ejecutarComandoGit(comandoCommit, directorio, "", &codigoSalida);
  std::string salidaCommit = filtrarLogSensitive(salidaCommitRaw);
  salidaCompleta << salidaCommit;

  bool commit_ok = (codigoSalida == 0);
  bool cambios_detectados = true;
  
  if (!commit_ok) {
    std::string lc = salidaCommit;
    for (auto &c : lc) c = (char)tolower(c);
    // Detectar tanto en inglés como en español (aunque ahora forzamos inglés)
    if (lc.find("nothing to commit") != std::string::npos || 
        lc.find("no changes added to commit") != std::string::npos ||
        lc.find("nada para hacer commit") != std::string::npos ||
        lc.find("no hay cambios para agregar") != std::string::npos) {
      commit_ok = true;
      cambios_detectados = false;  // No hay cambios para subir
      salidaCompleta << "\n✓ (Sin cambios nuevos para commitear)\n";
    }
  }

  if (!commit_ok) {
    resultado.mensaje = "Error al crear el commit";
    resultado.salidaCompleta = salidaCompleta.str();
    return resultado;
  }

  // Si no hay cambios, consideramos la operación exitosa (ya está sincronizado)
  if (!cambios_detectados) {
    resultado.exito = true;
    resultado.salidaCompleta = salidaCompleta.str();
    resultado.mensaje = "Repositorio sincronizado (sin cambios nuevos)";
    return resultado;
  }

  // Si hay cambios, proceder a push
  std::string ramaEfectiva = rama.empty() ? "main" : rama;
  std::string comandoPush = "push -u origin " + ramaEfectiva;
  std::string salidaPushRaw = ejecutarComandoGit(comandoPush, directorio, token, &codigoSalida);
  std::string salidaPush = filtrarLogSensitive(salidaPushRaw);
  salidaCompleta << salidaPush;

  resultado.salidaCompleta = salidaCompleta.str();
  resultado.exito = (codigoSalida == 0);
  resultado.mensaje = resultado.exito ? "Cambios subidos correctamente" : "Error al subir cambios (revise el log)";
  return resultado;
}

ResultadoOperacionGit GestorGit::obtenerEstado(const std::string &directorio) {
  ResultadoOperacionGit resultado{ false, "", "" };

  if (directorio.empty() || !fs::exists(fs::path(directorio) / ".git")) {
    resultado.mensaje = "No es un repositorio git válido";
    return resultado;
  }

  int codigoSalida = 0;
  std::string salida = ejecutarComandoGit("status -s -b", directorio, "", &codigoSalida);

  resultado.salidaCompleta = salida;
  resultado.exito = (codigoSalida == 0);
  resultado.mensaje = resultado.exito ? "Estado obtenido" : "Error obteniendo el estado del repositorio";
  return resultado;
}
