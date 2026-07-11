/*
 * PasswordObfuscator.hpp
 *
 *  Created on: 1 jul 2026
 *      Author: DjSteker
 *
 *  Módulo de ofuscación de contraseña por campo.
 *  Cada campo se ofusca diferente usando constantes secretas generadas en compilación.
 */

#ifndef PASSWORDOBFUSCATOR_HPP_
#define PASSWORDOBFUSCATOR_HPP_

#include <string>
#include <cstdint>

// ============================================================================
// CONSTANTES DE OFUSCACIÓN GENERADAS EN COMPILACIÓN
// Se definen mediante -D en el Makefile con valores aleatorios únicos
// ============================================================================


class PasswordObfuscator {
public:
  // =========================================================================
  // Métodos de ofuscación: transforman la contraseña de forma diferente
  // para cada campo usando los valores secretos únicos
  // =========================================================================

  /**
   * Ofusca contraseña para campo URL
   * Usa SECRET_NUMBER_URL + XOR + rotación
   */
  static std::string ofuscarURL(const std::string& password);

  /**
   * Ofusca contraseña para campo TOKEN
   * Usa SECRET_NUMBER_TOKEN + suma modular + shift
   */
  static std::string ofuscarToken(const std::string& password);

  /**
   * Ofusca contraseña para campo BRANCH
   * Usa SECRET_NUMBER_BRANCH + multiplicación + XOR
   */
  static std::string ofuscarBranch(const std::string& password);

  // =========================================================================
  // Métodos de desofuscación: invierten la transformación
  // =========================================================================

  static std::string desofuscarURL(const std::string& ofuscado);
  static std::string desofuscarToken(const std::string& ofuscado);
  static std::string desofuscarBranch(const std::string& ofuscado);

private:
  // Funciones auxiliares internas
  static std::string ofuscarGenerico(const std::string& password, uint32_t secretNumber, int metodo);
  static std::string desofuscarGenerico(const std::string& ofuscado, uint32_t secretNumber, int metodo);

  // Constantes de métodos internos
  static constexpr int METODO_XOR_ROTATE = 0;      // Para URL
  static constexpr int METODO_SUMA_SHIFT = 1;      // Para TOKEN
  static constexpr int METODO_MULT_XOR = 2;        // Para BRANCH
};

#endif /* PASSWORDOBFUSCATOR_HPP_ */
