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
  std::string rutaTemporal =
    "/tmp/.ghmgr_askpass_" + std::to_string(getpid()) + "_" + std::to_string(std::rand());

  std::ofstream archivoScript(rutaTemporal, std::ios::out | std::ios::trunc);
  if (!archivoScript.is_open()) {
    return "";
  }

  // El script simplemente devuelve el token cuando git le pide la
  // contraseña; el nombre de usuario es irrelevante para tokens de
  // GitHub (puede ser cualquier valor no vacío).
  archivoScript << "#!/bin/sh\n";
  archivoScript << "echo \"" << escaparParaComillasDobles(token) << "\"\n";
  archivoScript.close();

  std::error_code codigoError;
  fs::permissions(rutaTemporal,
                  fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec,
                  fs::perm_options::replace, codigoError);

  return rutaTemporal;
}

void GestorGit::eliminarScriptAskpass(const std::string &rutaScript) {
  if (!rutaScript.empty() && fs::exists(rutaScript)) {
    std::error_code codigoError;
    fs::remove(rutaScript, codigoError);
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

  int estado = pclose(tuberia);
  if (codigoSalida) {
    *codigoSalida = WEXITSTATUS(estado);
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
                                              const std::string &mensajeCommit, const std::string &token) {
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
  }

  std::string salidaAdd = ejecutarComandoGit("add -A", directorio, "", &codigoSalida);
  salidaCompleta << salidaAdd;

  std::string mensajeEfectivo = mensajeCommit.empty() ? "Actualización" : mensajeCommit;
  std::string comandoCommit = "commit -m \"" + escaparParaComillasDobles(mensajeEfectivo) + "\"";
  std::string salidaCommit = ejecutarComandoGit(comandoCommit, directorio, "", &codigoSalida);
  salidaCompleta << salidaCommit;

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
