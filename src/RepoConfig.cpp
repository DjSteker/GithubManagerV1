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

std::string GestorConfig::archivoPara(const std::string &dir) {
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256((unsigned char *)dir.c_str(), dir.size(), hash);
  std::ostringstream ss;
  for (int i = 0; i < 16; i++) {
    ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
  }
  return rutaBase() + "/" + ss.str() + ".xml";
}

// ============================================================================
// GUARDAR: Ofuscar TODO antes de escribir XML
// ============================================================================
bool GestorConfig::guardar(const ConfigRepo &cfg) {
  XMLDocument doc;
  XMLElement *root = doc.NewElement("repositorio");
  doc.InsertFirstChild(root);

  // URL: OFUSCAR con método XOR+ROTATE
  {
    std::string url_ofuscado = PasswordObfuscator::ofuscarURL(cfg.url);
    XMLElement *e = doc.NewElement("url");
    e->SetText(url_ofuscado.c_str());
    root->InsertEndChild(e);
  }

  // DIRECTORIO: OFUSCAR también (aunque no sea secreto, por uniformidad)
  {
    std::string dir_ofuscado = PasswordObfuscator::ofuscarBranch(cfg.directorio);
    XMLElement *e = doc.NewElement("directorio");
    e->SetText(dir_ofuscado.c_str());
    root->InsertEndChild(e);
  }

  // RAMA: OFUSCAR con método MULTIPLICACIÓN+XOR
  {
    std::string rama_ofuscada = PasswordObfuscator::ofuscarBranch(cfg.rama);
    XMLElement *e = doc.NewElement("rama");
    e->SetText(rama_ofuscada.c_str());
    root->InsertEndChild(e);
  }

  // TOKEN: CIFRADO FUERTE primero, LUEGO ofuscar (dobbel capa)
  // IMPORTANTE: cfg.tokenEncriptado YA viene cifrado desde Cifrado::encriptar()
  // Ahora le añadimos ofuscación como capa extra
  {
    if (!cfg.tokenEncriptado.empty()) {
      std::string token_doble_proteccion = PasswordObfuscator::ofuscarToken(cfg.tokenEncriptado);
      XMLElement *e = doc.NewElement("token");
      e->SetText(token_doble_proteccion.c_str());
      root->InsertEndChild(e);
    }
  }

  return doc.SaveFile(archivoPara(cfg.directorio).c_str()) == XML_SUCCESS;
}

// ============================================================================
// CARGAR: Desofuscar TODO al leer XML
// ============================================================================
bool GestorConfig::cargar(const std::string &dir, ConfigRepo &cfg) {
  XMLDocument doc;
  if (doc.LoadFile(archivoPara(dir).c_str()) != XML_SUCCESS) {
    return false;
  }

  XMLElement *root = doc.FirstChildElement("repositorio");
  if (!root) {
    return false;
  }

  auto getText = [](XMLElement *el) -> std::string {
    return el && el->GetText() ? el->GetText() : "";
  };

  // URL: DESOFUSCAR (inverso del guardado)
  {
    std::string url_ofuscado = getText(root->FirstChildElement("url"));
    cfg.url = PasswordObfuscator::desofuscarURL(url_ofuscado);
  }

  // DIRECTORIO: DESOFUSCAR
  {
    std::string dir_ofuscado = getText(root->FirstChildElement("directorio"));
    cfg.directorio = PasswordObfuscator::desofuscarBranch(dir_ofuscado);
  }

  // RAMA: DESOFUSCAR
  {
    std::string rama_ofuscada = getText(root->FirstChildElement("rama"));
    cfg.rama = PasswordObfuscator::desofuscarBranch(rama_ofuscada);
    if (cfg.rama.empty()) {
      cfg.rama = "main";
    }
  }

  // TOKEN: DESOFUSCAR primero, luego se desencriptará fuera
  // Orden INVERSO: guardar fue [CIFRAR → OFUSCAR], entonces cargar es [DESOFUSCAR → DESENCTIPTAR]
  {
    std::string token_double_protected = getText(root->FirstChildElement("token"));

    if (!token_double_protected.empty()) {
      // Paso 1: Quitar ofuscación
      std::string token_cifrado = PasswordObfuscator::desofuscarToken(token_double_protected);

      // Paso 2: Devolver el string cifrado (AES todavía intacto)
      // La UI lo recibirá como cfg.tokenEncriptado y deberá llamar a Cifrado::desencriptar()
      cfg.tokenEncriptado = token_cifrado;
    }
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
