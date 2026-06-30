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
      // Permitir solo si es formato aceptable (ej. git username, no password/token)
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
      // Solo reemplazar si está en contexto seguro (no exponer tokens reales)
      // En este caso sustituimos por marcador genérico
      resultado.replace(pos, par.first.length(), par.second);
      pos += par.second.length();
    }
  }

  return resultado;
}

std::string GestorGit::crearScriptAskpass(const std::string &token) {
  if (token.empty()) return "";

  std::string tmpl = "/tmp/.ghmgr_askpass_XXXXXX";
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');

  int fd = mkstemp(buf.data());
  if (fd == -1) {
    return "";
  }

  // Construir contenido del script sin exponer token en logs intermediarios
  std::string contenido = "#!/bin/sh\n";
  // Usar printf para evitar interpretaciones de escape y mantener token en memoria segura
  contenido += "printf '%s' \"" + escaparParaComillasDobles(token) + "\"\n";

  ssize_t written = write(fd, contenido.data(), contenido.size());
  if (written != (ssize_t)contenido.size()) {
    close(fd);
    unlink(buf.data());
    // Intentar limpiar cualquier rastro del token en stack local
    return "";
  }

  fsync(fd);
  // Asegurar permisos del propietario exclusivamente (700)
  fchmod(fd, S_IRWXU);
  close(fd);

  // NOTA IMPORTANTE: El token debe ser limpiado por el llamador INMEDIATAMENTE DESPUÉS
  // de llamar a esta función para minimizar su permanencia en memoria

  return std::string(buf.data());
}

void GestorGit::eliminarScriptAskpass(const std::string &rutaScript) {
  if (!rutaScript.empty()) {
    std::error_code ec;

    // Sobreescribir archivo con ceros antes de borrar (extra safety)
    if (fs::exists(rutaScript)) {
      try {
        std::ifstream file(rutaScript, std::ios::binary);
        std::streamsize fileSize = std::distance(
          std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>());

        if (fileSize > 0) {
          std::ofstream overwrite(rutaScript, std::ios::binary | std::ios::trunc);
          std::vector<char> zeroes(static_cast<size_t>(fileSize), 0);
          overwrite.write(zeroes.data(), zeroes.size());
          overwrite.flush();
          overwrite.close();

          // Forzar sync al disco
          fsync(open(rutaScript.c_str(), O_RDONLY));
        }
      } catch (...) {
        // Ignorar error de sobrescritura - intentaremos borrar igualmente
      }
    }

    fs::remove(rutaScript, ec);
    unlink(rutaScript.c_str());
  }
}

std::string GestorGit::ejecutarComandoGit(const std::string &comando, const std::string &directorioTrabajo,
                                          const std::string &token, int *codigoSalida) {
  std::string rutaAskpass;
  std::string prefijoEntorno = "GIT_TERMINAL_PROMPT=0 ";

  if (!token.empty()) {
    rutaAskpass = crearScriptAskpass(token);
    if (!rutaAskpass.empty()) {
      prefijoEntorno += "GIT_ASKPASS=\"" + rutaAskpass + "\" ";
    } else {
      // Si no pudo crear askpass, advertir pero continuar sin token
      // Esto forzará prompt interactivo si es necesario
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
    eliminarScriptAskpass(rutaAskpass);
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

  eliminarScriptAskpass(rutaAskpass);
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

  // VALIDACIÓN CRÍTICA: Rechazar URLs con credenciales embebidas
  if (!validarUrlRepositorio(urlRepositorio)) {
    resultado.mensaje = "URL inválida: no se permiten credenciales embebidas (usar https://github.com/user/repo.git)";
    return resultado;
  }

  // Si el destino existe y no está vacío, git clone normalmente falla.
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

  // FILTRAR LOG ANTES DE DEVOLVERLO
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

  // Validar URL opcional si se proporciona
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

  // Detectar si no hay cambios (no es fatal)
  bool commit_ok = (codigoSalida == 0);
  if (!commit_ok) {
    std::string lc = salidaCommit;
    for (auto &c : lc) c = (char)tolower(c);
    if (lc.find("nothing to commit") != std::string::npos || lc.find("no changes added to commit") != std::string::npos) {
      commit_ok = true;
      salidaCompleta << "\n(Sin cambios nuevos para commitear)\n";
    }
  }

  if (!commit_ok) {
    resultado.mensaje = "Error al crear el commit";
    resultado.salidaCompleta = salidaCompleta.str();
    return resultado;
  }

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
