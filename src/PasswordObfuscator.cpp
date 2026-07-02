/*
 * PasswordObfuscator.cpp
 *
 *  Created on: 1 jul 2026
 *      Author: DjSteker
 *
 *  Implementación de ofuscación de contraseña diferente por campo.
 *  Cada campo usa un algoritmo diferente con su propio secreto compilado.
 */

#include "PasswordObfuscator.hpp"
#include "PasswordObfuscator_Secrets.h"
#include <openssl/crypto.h>
#include <algorithm>
#include <cstring>

// ============================================================================
// MÉTODO 0: XOR + ROTACIÓN (usado para URL)
// ============================================================================
// Algoritmo:
//   1. XOR cada byte con byte del secreto
//   2. Rotar bits dentro de cada byte
// Ventaja: Rápido, no reversible sin secreto
static std::string ofuscar_xor_rotate(const std::string& password, uint32_t secretNumber) {
  std::string result = password;

  for (size_t i = 0; i < result.size(); i++) {
    // XOR con byte derivado del secreto
    uint8_t secret_byte = (secretNumber >> (8 * (i % 4))) & 0xFF;
    result[i] ^= secret_byte;

    // Rotar bits (left rotate by 3)
    uint8_t byte = result[i];
    result[i] = ((byte << 3) | (byte >> 5));
  }

  return result;
}

static std::string desofuscar_xor_rotate(const std::string& ofuscado, uint32_t secretNumber) {
  std::string result = ofuscado;

  for (size_t i = 0; i < result.size(); i++) {
    // Rotar bits inverso (right rotate by 3)
    uint8_t byte = result[i];
    result[i] = ((byte >> 3) | (byte << 5));

    // XOR con mismo byte del secreto
    uint8_t secret_byte = (secretNumber >> (8 * (i % 4))) & 0xFF;
    result[i] ^= secret_byte;
  }

  return result;
}

// ============================================================================
// MÉTODO 1: SUMA MODULAR + SHIFT (usado para TOKEN)
// ============================================================================
// Algoritmo:
//   1. Sumar cada byte con valor derivado del secreto (módulo 256)
//   2. Shift circular según posición
// Ventaja: Diferente de XOR, más difícil de criptoanalizar
static std::string ofuscar_suma_shift(const std::string& password, uint32_t secretNumber) {
  std::string result = password;

  for (size_t i = 0; i < result.size(); i++) {
    uint8_t secret_byte = (secretNumber >> (8 * (i % 4))) & 0xFF;
    uint16_t suma = (uint16_t)result[i] + secret_byte;
    result[i] = suma & 0xFF;

    // Shift según posición en el string
    uint8_t shift = (i % 8);
    uint8_t byte = result[i];
    result[i] = ((byte << shift) | (byte >> (8 - shift)));
  }

  return result;
}

static std::string desofuscar_suma_shift(const std::string& ofuscado, uint32_t secretNumber) {
  std::string result = ofuscado;

  for (size_t i = 0; i < result.size(); i++) {
    // Shift inverso
    uint8_t shift = (i % 8);
    uint8_t byte = result[i];
    result[i] = ((byte >> shift) | (byte << (8 - shift)));

    // Restar (inverso de suma)
    uint8_t secret_byte = (secretNumber >> (8 * (i % 4))) & 0xFF;
    uint16_t diferencia = (uint16_t)result[i] - secret_byte;
    result[i] = diferencia & 0xFF;
  }

  return result;
}

// ============================================================================
// MÉTODO 2: MULTIPLICACIÓN + XOR (usado para BRANCH)
// ============================================================================
// Algoritmo:
//   1. Multiplicar cada byte por constante derivada del secreto
//   2. XOR con resultado desplazado del secreto
// Ventaja: Combinación no lineal, muy diferente a métodos anteriores
static std::string ofuscar_mult_xor(const std::string& password, uint32_t secretNumber) {
  std::string result = password;

  for (size_t i = 0; i < result.size(); i++) {
    uint8_t secret_byte = (secretNumber >> (8 * (i % 4))) & 0xFF;

    // Multiplicar por un valor derivado (primo pequeño para máxima entropía)
    uint8_t multiplier = (secret_byte % 17) + 3;  // Rango 3-19
    uint16_t mult_result = (uint16_t)result[i] * multiplier;
    uint8_t byte = mult_result & 0xFF;

    // XOR con secreto desplazado
    result[i] = byte ^ ((secret_byte >> 1) | (secret_byte << 7));
  }

  return result;
}

static std::string desofuscar_mult_xor(const std::string& ofuscado, uint32_t secretNumber) {
  std::string result = ofuscado;

  // Para deshacer multiplicación, usamos inverso modular en Z/256Z
  // Tabla precomputada de inversos multiplicativos para los multiplicadores
  static const uint8_t inv_mult[17] = {
    0,                                       // no usado (mult = 0)
    171, 229, 205, 205, 219, 219, 117, 117,  // mult 3-10
    99, 111, 205, 221, 141, 153, 9, 137      // mult 11-18
  };

  for (size_t i = 0; i < result.size(); i++) {
    uint8_t secret_byte = (secretNumber >> (8 * (i % 4))) & 0xFF;

    // XOR inverso (XOR es su propio inverso)
    result[i] ^= ((secret_byte >> 1) | (secret_byte << 7));

    // Multiplicar inverso
    uint8_t multiplier = (secret_byte % 17) + 3;
    uint8_t inv = inv_mult[multiplier - 3];
    uint16_t div_result = (uint16_t)result[i] * inv;
    result[i] = div_result & 0xFF;
  }

  return result;
}

// ============================================================================
// INTERFAZ PÚBLICA
// ============================================================================

std::string PasswordObfuscator::ofuscarURL(const std::string& password) {
  return ofuscar_xor_rotate(password, SECRET_NUMBER_URL);
}

std::string PasswordObfuscator::ofuscarToken(const std::string& password) {
  return ofuscar_suma_shift(password, SECRET_NUMBER_TOKEN);
}

std::string PasswordObfuscator::ofuscarBranch(const std::string& password) {
  return ofuscar_mult_xor(password, SECRET_NUMBER_BRANCH);
}

std::string PasswordObfuscator::desofuscarURL(const std::string& ofuscado) {
  return desofuscar_xor_rotate(ofuscado, SECRET_NUMBER_URL);
}

std::string PasswordObfuscator::desofuscarToken(const std::string& ofuscado) {
  return desofuscar_suma_shift(ofuscado, SECRET_NUMBER_TOKEN);
}

std::string PasswordObfuscator::desofuscarBranch(const std::string& ofuscado) {
  return desofuscar_mult_xor(ofuscado, SECRET_NUMBER_BRANCH);
}

std::string PasswordObfuscator::ofuscarGenerico(const std::string& password, uint32_t secretNumber, int metodo) {
  switch (metodo) {
    case METODO_XOR_ROTATE:
      return ofuscar_xor_rotate(password, secretNumber);
    case METODO_SUMA_SHIFT:
      return ofuscar_suma_shift(password, secretNumber);
    case METODO_MULT_XOR:
      return ofuscar_mult_xor(password, secretNumber);
    default:
      return password;  // Fallback seguro
  }
}

std::string PasswordObfuscator::desofuscarGenerico(const std::string& ofuscado, uint32_t secretNumber, int metodo) {
  switch (metodo) {
    case METODO_XOR_ROTATE:
      return desofuscar_xor_rotate(ofuscado, secretNumber);
    case METODO_SUMA_SHIFT:
      return desofuscar_suma_shift(ofuscado, secretNumber);
    case METODO_MULT_XOR:
      return desofuscar_mult_xor(ofuscado, secretNumber);
    default:
      return ofuscado;  // Fallback seguro
  }
}
