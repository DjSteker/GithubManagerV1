/*
 * RepoConfig.hpp
 *
 *  Created on: 30 jun 2026
 *      Author: DjSteker
 */

#ifndef REPOCONFIG_HPP_
#define REPOCONFIG_HPP_

#include <string>
#include <vector>

struct ConfigRepo {
  std::string url;               // Se guarda ofuscado en XML
  std::string directorio;        // Se guarda ofuscado en XML (campo visible)
  std::string directorioOriginal;// Path REAL sin ofuscar (SOLO para hash lookup interno)
  std::string rama = "main";     // Se guarda ofuscado en XML
  std::string tokenEncriptado;   // Cifrado AES puro, sin ofuscación adicional

  // Helper para inicialización
  ConfigRepo() : rama("main") {}
};

class GestorConfig {
public:
  static std::string rutaBase();
  static std::string archivoPara(const std::string& directorio_real);  // Usa directorioOriginal
  static bool guardar(const ConfigRepo& cfg);                         // Guarda todo excepto directorioOriginal
  static bool cargar(const std::string& directorioReal, ConfigRepo& cfg);  // Pasa directorioReal para lookup
  static std::vector<std::string> listarRepos();
};

#endif /* REPOCONFIG_HPP_ */
