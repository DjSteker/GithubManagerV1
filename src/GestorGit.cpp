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

namespace fs = std::filesystem;

static std::string escaparParaComillasDobles(const std::string &texto) {
  std::string resultado;
  resultado.reserve(texto.size());
  for (char caracter : texto) {
    if (caracter == '"' || caracter == '\\' || caracter == '$' || caracter == '`') {
      resultado += '\\';
    }
    resultado += caracter;
  }
  return resultado;
}

std::string GestorGit::crearScriptAskpass(const std::string &token) {
  std::string tmpl = "/tmp/.ghmgr_askpass_XXXXXX";
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');

  int fd = mkstemp(buf.data());
  if (fd == -1) {
    return "";
  }

  // Construir el contenido con cuidado (evitar inyección):
  std::string contenido = "#!/bin/sh\n";
  // Usar printf para evitar interpretaciones de escape
  contenido += "printf '%s' \"" + escaparParaComillasDobles(token) + "\"\n";

  ssize_t written = write(fd, contenido.data(), contenido.size());
  if (written != (ssize_t)contenido.size()) {
    close(fd);
    unlink(buf.data());
    return "";
  }

  fsync(fd);
  // Asegurar permisos del propietario sólo (700)
  fchmod(fd, S_IRWXU);
  close(fd);

  return std::string(buf.data());
}

void GestorGit::eliminarScriptAskpass(const std::string &rutaScript) {
  if (!rutaScript.empty() && fs::exists(rutaScript)) {
    std::error_code ec;
    fs::remove(rutaScript, ec);
    // Asegurar eliminación por si unlink directo es necesario
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

  // Si el destino existe y no está vacío, git clone normalmente falla.
  // Mejor avisar y fallar claramente para que la app pueda decidir (o usar init+remote).
  if (fs::exists(directorioDestino)) {
    bool vacio = true;
    for (auto &p : fs::directory_iterator(directorioDestino)) { vacio = false; break; }
    if (!vacio) {
      resultado.mensaje = "El directorio destino existe y no está vacío; no se puede clonar allí. Usa una carpeta vacía o inicializa el repo local.";
      resultado.exito = false;
      return resultado;
    }
  }

  std::string comando = "clone \"" + escaparParaComillasDobles(urlRepositorio) + "\" \"" + escaparParaComillasDobles(directorioDestino) + "\"";

  int codigoSalida = 0;
  std::string salida = ejecutarComandoGit(comando, "", token, &codigoSalida);

  resultado.salidaCompleta = salida;
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
  std::string salida = ejecutarComandoGit(comando, directorio, token, &codigoSalida);

  resultado.salidaCompleta = salida;
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
  std::string salidaCommit = ejecutarComandoGit(comandoCommit, directorio, "", &codigoSalida);
  salidaCompleta << salidaCommit;

  // Si commit devolvió fallo, comprobar si fue porque no había cambios
  bool commit_ok = (codigoSalida == 0);
  if (!commit_ok) {
    // detectar mensajes comunes "nothing to commit" o "no changes added to commit"
    std::string lc = salidaCommit;
    for (auto &c : lc) c = (char)tolower(c);
    if (lc.find("nothing to commit") != std::string::npos || lc.find("no changes added to commit") != std::string::npos) {
      // no hay cambios: no es fatal, continuar con push (quizá no haya nada que subir)
      commit_ok = true;
    }
  }

  if (!commit_ok) {
    resultado.mensaje = "Error al crear el commit";
    resultado.salidaCompleta = salidaCompleta.str();
    return resultado;
  }

  std::string ramaEfectiva = rama.empty() ? "main" : rama;
  std::string comandoPush = "push -u origin " + ramaEfectiva;
  std::string salidaPush = ejecutarComandoGit(comandoPush, directorio, token, &codigoSalida);
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
