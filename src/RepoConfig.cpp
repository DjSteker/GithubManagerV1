/*
 * RepoConfig.cpp
 *
 *  Created on: 30 jun 2026
 *      Author: usuario001
 */

#include "RepoConfig.hpp"
#include "tinyxml2.h"
#include <filesystem>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;
using namespace tinyxml2;

std::string GestorConfig::rutaBase() {
  const char* home = getenv("HOME");
  std::string p = std::string(home ? home : ".") + "/.config/gestorgit/repos";
  fs::create_directories(p);
  return p;
}

std::string GestorConfig::archivoPara(const std::string& dir) {
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256((unsigned char*)dir.c_str(), dir.size(), hash);
  std::ostringstream ss;
  for (int i = 0; i < 8; i++) {ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];}
  return rutaBase() + "/" + ss.str() + ".xml";
}

bool GestorConfig::guardar(const ConfigRepo& cfg) {
  XMLDocument doc;
  XMLElement* root = doc.NewElement("repositorio");
  doc.InsertFirstChild(root);

  XMLElement* e = doc.NewElement("url");
  e->SetText(cfg.url.c_str());
  root->InsertEndChild(e);

  e = doc.NewElement("directorio");
  e->SetText(cfg.directorio.c_str());
  root->InsertEndChild(e);

  e = doc.NewElement("rama");
  e->SetText(cfg.rama.c_str());
  root->InsertEndChild(e);

  e = doc.NewElement("token");
  e->SetText(cfg.tokenEncriptado.c_str());
  root->InsertEndChild(e);

  return doc.SaveFile(archivoPara(cfg.directorio).c_str()) == XML_SUCCESS;
}

bool GestorConfig::cargar(const std::string& dir, ConfigRepo& cfg) {
  XMLDocument doc;
  if (doc.LoadFile(archivoPara(dir).c_str()) != XML_SUCCESS) {return false;}

  XMLElement* root = doc.FirstChildElement("repositorio");
  if (!root) {return false;}

  auto getText = [](XMLElement* el) -> std::string {
    return el && el->GetText() ? el->GetText() : "";
  };

  cfg.url = getText(root->FirstChildElement("url"));
  cfg.directorio = getText(root->FirstChildElement("directorio"));
  cfg.rama = getText(root->FirstChildElement("rama"));
  if (cfg.rama.empty()) {cfg.rama = "main";}
  cfg.tokenEncriptado = getText(root->FirstChildElement("token"));

  return true;
}

std::vector<std::string> GestorConfig::listarRepos() {
  std::vector<std::string> v;
  if (!fs::exists(rutaBase())) {return v;}
  for (auto& e : fs::directory_iterator(rutaBase())) {
    if (e.path().extension() == ".xml") {v.push_back(e.path().string());}}
  return v;
}
