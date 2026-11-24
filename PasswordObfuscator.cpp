/*
 * PasswordObfuscator.cpp
 */

#include "PasswordObfuscator.hpp"
#include "PasswordObfuscator_Secrets.h"
#include <openssl/crypto.h>
#include <cstdint>

// ============================================================================
// MÉTODO 0: XOR + ROTACIÓN (URL)
// ============================================================================
static std::string ofuscar_xor_rotate(const std::string& password, uint32_t secretNumber) {
  std::string result = password;
  for (size_t i = 0; i < result.size(); i++) {
    uint8_t secret_byte = static_cast<uint8_t>((secretNumber >> (8 * (i % 4))) & 0xFF);
    result[i] ^= secret_byte;
    uint8_t byte = static_cast<uint8_t>(result[i]);
    result[i] = static_cast<uint8_t>(((byte << 3) | (byte >> 5)) & 0xFF);
  }
  return result;
}

static std::string desofuscar_xor_rotate(const std::string& ofuscado, uint32_t secretNumber) {
  std::string result = ofuscado;
  for (size_t i = 0; i < result.size(); i++) {
    uint8_t byte = static_cast<uint8_t>(result[i]);
    result[i] = static_cast<uint8_t>(((byte >> 3) | (byte << 5)) & 0xFF);
    uint8_t secret_byte = static_cast<uint8_t>((secretNumber >> (8 * (i % 4))) & 0xFF);
    result[i] ^= secret_byte;
  }
  return result;
}

// ============================================================================
// MÉTODO 1: SUMA + SHIFT (TOKEN)
// ============================================================================
static std::string ofuscar_suma_shift(const std::string& password, uint32_t secretNumber) {
  std::string result = password;
  for (size_t i = 0; i < result.size(); i++) {
    uint8_t secret_byte = static_cast<uint8_t>((secretNumber >> (8 * (i % 4))) & 0xFF);
    result[i] = static_cast<uint8_t>(result[i] + secret_byte);
    uint8_t shift_val = static_cast<uint8_t>(i % 8);
    uint8_t byte = static_cast<uint8_t>(result[i]);
    if (shift_val > 0 && shift_val < 8) {
      result[i] = static_cast<uint8_t>(((byte << shift_val) | (byte >> (8 - shift_val))) & 0xFF);
    }
  }
  return result;
}

static std::string desofuscar_suma_shift(const std::string& ofuscado, uint32_t secretNumber) {
  std::string result = ofuscado;
  for (size_t i = 0; i < result.size(); i++) {
    uint8_t shift_val = static_cast<uint8_t>(i % 8);
    uint8_t byte = static_cast<uint8_t>(result[i]);
    if (shift_val > 0 && shift_val < 8) {
      result[i] = static_cast<uint8_t>(((byte >> shift_val) | (byte << (8 - shift_val))) & 0xFF);
    }
    uint8_t secret_byte = static_cast<uint8_t>((secretNumber >> (8 * (i % 4))) & 0xFF);
    result[i] = static_cast<uint8_t>(result[i] - secret_byte);
  }
  return result;
}

// ============================================================================
// MÉTODO 2: XOR + ROTATE + ADD (BRANCH) - VERSION CORREGIDA
// ============================================================================
static std::string ofuscar_branch_fixed(const std::string& password, uint32_t secretNumber) {
  std::string result = password;
  for (size_t i = 0; i < result.size(); i++) {
    uint8_t secret_byte = static_cast<uint8_t>((secretNumber >> (8 * (i % 4))) & 0xFF);
    result[i] ^= secret_byte;
    uint8_t byte = static_cast<uint8_t>(result[i]);
    result[i] = static_cast<uint8_t>(((byte << 3) | (byte >> 5)) & 0xFF);
    result[i] = static_cast<uint8_t>(result[i] + (static_cast<uint8_t>(i & 0xFF) + 1));
  }
  return result;
}

static std::string desofuscar_branch_fixed(const std::string& ofuscado, uint32_t secretNumber) {
  std::string result = ofuscado;
  for (size_t i = 0; i < result.size(); i++) {
    uint8_t secret_byte = static_cast<uint8_t>((secretNumber >> (8 * (i % 4))) & 0xFF);
    result[i] = static_cast<uint8_t>(result[i] - (static_cast<uint8_t>(i & 0xFF) + 1));
    uint8_t byte = static_cast<uint8_t>(result[i]);
    result[i] = static_cast<uint8_t>(((byte >> 3) | (byte << 5)) & 0xFF);
    result[i] ^= secret_byte;
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
  return ofuscar_branch_fixed(password, SECRET_NUMBER_BRANCH);
}

std::string PasswordObfuscator::desofuscarURL(const std::string& ofuscado) {
  return desofuscar_xor_rotate(ofuscado, SECRET_NUMBER_URL);
}

std::string PasswordObfuscator::desofuscarToken(const std::string& ofuscado) {
  return desofuscar_suma_shift(ofuscado, SECRET_NUMBER_TOKEN);
}

std::string PasswordObfuscator::desofuscarBranch(const std::string& ofuscado) {
  return desofuscar_branch_fixed(ofuscado, SECRET_NUMBER_BRANCH);
}

std::string PasswordObfuscator::ofuscarGenerico(const std::string& password, uint32_t secretNumber, int metodo) {
  switch (metodo) {
    case 0: return ofuscar_xor_rotate(password, secretNumber);
    case 1: return ofuscar_suma_shift(password, secretNumber);
    case 2: return ofuscar_branch_fixed(password, secretNumber);
    default: return password;
  }
}

std::string PasswordObfuscator::desofuscarGenerico(const std::string& ofuscado, uint32_t secretNumber, int metodo) {
  switch (metodo) {
    case 0: return desofuscar_xor_rotate(ofuscado, secretNumber);
    case 1: return desofuscar_suma_shift(ofuscado, secretNumber);
    case 2: return desofuscar_branch_fixed(ofuscado, secretNumber);
    default: return ofuscado;
  }
}
