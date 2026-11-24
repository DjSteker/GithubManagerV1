/*
 * Cifrado.hpp
 *
 *  Created on: 30 jun 2026
 *      Author: DjSteker
 */

#ifndef CIFRADO_HPP_
#define CIFRADO_HPP_


#include <string>

class Cifrado {
public:
  static std::string encriptar(const std::string& textoPlano, const std::string& password);
  static std::string desencriptar(const std::string& textoCifradoB64, const std::string& password);

  // Generar HMAC-SHA256 como checksum independiente
  // Usa algoritmo diferente (HMAC) al de encriptación (AES-256-GCM)
  static std::string generarChecksum(const std::string& datos, const std::string& claveSecreta);
  static bool verificarChecksum(const std::string& datos, const std::string& checksumExpectado, const std::string& claveSecreta);
};


#endif /* CIFRADO_HPP_ */
