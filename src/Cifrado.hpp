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
};


#endif /* CIFRADO_HPP_ */
