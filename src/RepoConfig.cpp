/*
 * RepoConfig.cpp
 */

#include "RepoConfig.hpp"
#include "Cifrado.hpp"
#include "PasswordObfuscator.hpp"
#include "tinyxml2.h"
#include <filesystem>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;
using namespace tinyxml2;

std::string GestorConfig::rutaBase() {
  const char *xdg = getenv("XDG_CONFIG_HOME");
  const char *home = getenv("HOME");
  std::string base;
  if (xdg && xdg[0] != '\0') {
    base = std::string(xdg) + "/gestorgit/repos";
  } else if (home && home[0] != '\0') {
    base = std::string(home) + "/.config/gestorgit/repos";
  } else {
    base = "./.config/gestorgit/repos";
  }

  std::error_code ec;
  fs::create_directories(base, ec);
  return base;
}

std::string GestorConfig::archivoPara(const std::string &dirReal) {
  // USAR DIRECTORIO ORIGINAL SIN OFUSCAR PARA GENERAR HASH
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256((unsigned char *)dirReal.c_str(), dirReal.size(), hash);
  std::ostringstream ss;
  for (int i = 0; i < 16; i++) {
    ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
  }
  return rutaBase() + "/" + ss.str() + ".xml";
}

bool GestorConfig::guardar(const ConfigRepo &cfg) {
  // Verificar que tenemos el directorio original para el hash
  std::string dirParaHash = cfg.directorioOriginal.empty() ? cfg.directorio : cfg.directorioOriginal;

  XMLDocument doc;
  XMLElement *root = doc.NewElement("repositorio");
  doc.InsertFirstChild(root);

  // URL: OFUSCAR
  {
    std::string url_ofuscado = PasswordObfuscator::ofuscarURL(cfg.url);
    XMLElement *e = doc.NewElement("url");
    e->SetText(url_ofuscado.c_str());
    root->InsertEndChild(e);
  }

  // DIRECTORIO: OFUSCAR (para el contenido XML)
  {
    // Si cfg.directorio ya viene ofuscado desde GtkInterface, no volver a ofuscar
    // Si llega plano, ofuscarlo ahora
    std::string dir_final = cfg.directorio;
    // Detección simple: si contiene /home/ u otros patrones comunes de paths, asumimos que YA está ofuscado
    // Mejor: verificar longitud - paths ofuscados suelen ser similar longitud
    std::string dir_ofuscado = PasswordObfuscator::ofuscarBranch(dir_final);

    XMLElement *e = doc.NewElement("directorio");
    e->SetText(dir_ofuscado.c_str());
    root->InsertEndChild(e);
  }

  // RAMA: OFUSCAR
  {
    std::string rama_ofuscada = PasswordObfuscator::ofuscarBranch(cfg.rama);
    XMLElement *e = doc.NewElement("rama");
    e->SetText(rama_ofuscada.c_str());
    root->InsertEndChild(e);
  }

  // TOKEN: CIFRADO PURO (sin ofuscación adicional - era error anterior)
  {
    if (!cfg.tokenEncriptado.empty()) {
      XMLElement *e = doc.NewElement("token");
      e->SetText(cfg.tokenEncriptado.c_str());
      root->InsertEndChild(e);
    }
  }

  return doc.SaveFile(archivoPara(dirParaHash).c_str()) == XML_SUCCESS;
}

bool GestorConfig::cargar(const std::string &dirReal, ConfigRepo &cfg) {
  // File path generado desde directorio REAL (no ofuscado)
  std::string filePath = archivoPara(dirReal);

  XMLDocument doc;
  if (doc.LoadFile(filePath.c_str()) != XML_SUCCESS) {
    return false;
  }

  XMLElement *root = doc.FirstChildElement("repositorio");
  if (!root) {
    return false;
  }

  auto getText = [](XMLElement *el) -> std::string {
    return el && el->GetText() ? el->GetText() : "";
  };

  // URL: DESOFUSCAR
  {
    std::string url_ofuscado = getText(root->FirstChildElement("url"));
    cfg.url = PasswordObfuscator::desofuscarURL(url_ofuscado);
  }

  // DIRECTORIO: DESOFUSCAR (para mostrar en UI)
  {
    std::string dir_ofuscado = getText(root->FirstChildElement("directorio"));
    cfg.directorio = PasswordObfuscator::desofuscarBranch(dir_ofuscado);
  }

  // DIRECTORIORIGINAL: Mantener el que pasamos como parámetro (ya sabemos la verdad)
  cfg.directorioOriginal = dirReal;

  // RAMA: DESOFUSCAR
  {
    std::string rama_ofuscada = getText(root->FirstChildElement("rama"));
    cfg.rama = PasswordObfuscator::desofuscarBranch(rama_ofuscada);
    if (cfg.rama.empty()) {
      cfg.rama = "main";
    }
  }

  // TOKEN: PLANO Base64 de AES (sin desofuscar)
  {
    cfg.tokenEncriptado = getText(root->FirstChildElement("token"));
  }

  return true;
}

std::vector<std::string> GestorConfig::listarRepos() {
  std::vector<std::string> v;
  if (!fs::exists(rutaBase())) {
    return v;
  }
  for (auto &e : fs::directory_iterator(rutaBase())) {
    if (e.path().extension() == ".xml") {
      v.push_back(e.path().string());
    }
  }
  return v;
}
