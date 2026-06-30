/*
 * GestorGit.hpp
 *
 *  Created on: 30 jun 2026
 *      Author: DjSteker
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
  static ResultadoOperacionGit clonarRepositorio(const std::string &urlRepositorio,
                                                 const std::string &directorioDestino,
                                                 const std::string &token);

  static ResultadoOperacionGit bajarCambios(const std::string &directorio,
                                            const std::string &rama,
                                            const std::string &token);

  // Nota: urlOpcional se usa cuando se inicializa un repo vacío y queremos agregar remote origin
  static ResultadoOperacionGit subirCambios(const std::string &directorio, const std::string &rama,
                                            const std::string &mensajeCommit, const std::string &token,
                                            const std::string &urlOpcional = "");

  static ResultadoOperacionGit obtenerEstado(const std::string &directorio);

  // Métodos auxiliares de validación
  static bool validarUrlRepositorio(const std::string &url);
  static std::string filtrarLogSensitive(const std::string &log);

private:
  static std::string crearScriptAskpass(const std::string &token);
  static void eliminarScriptAskpass(const std::string &rutaScript);
  static std::string ejecutarComandoGit(const std::string &comando, const std::string &directorioTrabajo,
                                        const std::string &token, int *codigoSalida);
};

#endif /* GESTORGIT_HPP_ */
