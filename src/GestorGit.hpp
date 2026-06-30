/*
 * GestorGit.hpp
 *
 *  Created on: 30 jun 2026
 *      Author: usuario001
 */

#ifndef GESTORGIT_HPP_
#define GESTORGIT_HPP_


#include <string>

struct ResultadoOperacionGit {
  bool exito;
  std::string mensaje;
  std::string salidaCompleta;
};

// Envuelve las operaciones git necesarias (clonar, bajar/pull, subir/push,
// estado). El token, cuando se proporciona, se inyecta mediante un script
// GIT_ASKPASS temporal de un solo uso, en vez de incrustarlo en la línea de
// comandos, para que no quede expuesto en la lista de procesos del sistema.
class GestorGit {
public:
  static ResultadoOperacionGit clonarRepositorio(const std::string &urlRepositorio, const std::string &directorioDestino, const std::string &token);
  static ResultadoOperacionGit bajarCambios(const std::string &directorio, const std::string &rama, const std::string &token);
  static ResultadoOperacionGit subirCambios(const std::string &directorio, const std::string &rama, const std::string &mensajeCommit, const std::string &token);
  static ResultadoOperacionGit obtenerEstado(const std::string &directorio);

private:
  static std::string crearScriptAskpass(const std::string &token);
  static void eliminarScriptAskpass(const std::string &rutaScript);
  static std::string ejecutarComandoGit(const std::string &comando, const std::string &directorioTrabajo, const std::string &token, int *codigoSalida);
};

#endif /* GESTORGIT_HPP_ */
