/*
 * RepoConfig.hpp
 *
 *  Created on: 30 jun 2026
 *      Author: usuario001
 */

#ifndef REPOCONFIG_HPP_
#define REPOCONFIG_HPP_


#include <string>
#include <vector>

struct ConfigRepo {
  std::string url;
  std::string directorio;
  std::string rama = "main";
  std::string tokenEncriptado;  // en XML
};

class GestorConfig {
public:
  static std::string rutaBase();
  static std::string archivoPara(const std::string& directorio);
  static bool guardar(const ConfigRepo& cfg);
  static bool cargar(const std::string& directorio, ConfigRepo& cfg);
  static std::vector<std::string> listarRepos();
};


#endif /* REPOCONFIG_HPP_ */

