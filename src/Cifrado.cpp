/*
 * Cifrado.cpp
 *
 *  Created on: 30 jun 2026
 *      Author: DjSteker
 */

#include "Cifrado.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/crypto.h>
#include <vector>
#include <stdexcept>
#include <string>
#include <cstring>

// Base64 helpers
static std::string b64_encode(const unsigned char *d, int len) {
  if (len <= 0) {return "";}
  int out_len = 4 * ((len + 2) / 3);
  std::string out(out_len + 1, '\0');
  int r = EVP_EncodeBlock((unsigned char *)out.data(), d, len);
  if (r < 0) {throw std::runtime_error("Base64 encode error");}
  out.resize(r);
  return out;
}

static std::vector<unsigned char> b64_decode(const std::string &in) {
  if (in.empty()) { return {}; }
  std::vector<unsigned char> out(in.size());
  int r = EVP_DecodeBlock(out.data(), (const unsigned char *)in.c_str(), in.size());
  if (r < 0) {throw std::runtime_error("Base64 decode error");}
  out.resize(r);
  while (!out.empty() && out.back() == 0) {out.pop_back();}
  return out;
}

// MEJORA CRÍTICA: Iteraciones aumentadas de 10,000 a 100,000
constexpr int ITERACIONES_PBKDF2 = 100000;

std::string Cifrado::encriptar(const std::string &txt, const std::string &pass) {
  // Copias locales mutables para poder limpiarlas
  std::string texto = txt;
  std::string pass_copy = pass;

  // Blob: salt(16) | iv(12) | tag(16) | ciphertext(...)
  unsigned char salt[16], iv[12];
  if (RAND_bytes(salt, sizeof(salt)) != 1) {
    if (!pass_copy.empty()) {
      OPENSSL_cleanse(&pass_copy[0], pass_copy.size());
      pass_copy.clear();
    }
    throw std::runtime_error("RAND_bytes(salt) failed");
  }
  if (RAND_bytes(iv, sizeof(iv)) != 1) {
    if (!pass_copy.empty()) {
      OPENSSL_cleanse(&pass_copy[0], pass_copy.size());
      pass_copy.clear();
    }
    throw std::runtime_error("RAND_bytes(iv) failed");
  }

  unsigned char key[32];
  // AUMENTO DE ITERACIONES PARA MAYOR SEGURIDAD
  if (!PKCS5_PBKDF2_HMAC(pass_copy.c_str(), (int)pass_copy.size(), salt, sizeof(salt), ITERACIONES_PBKDF2, EVP_sha256(), sizeof(key), key)) {
    if (!pass_copy.empty()) {
      OPENSSL_cleanse(&pass_copy[0], pass_copy.size());
      pass_copy.clear();
    }
    memset(key, 0, sizeof(key));  // Limpieza adicional
    throw std::runtime_error("PBKDF2 key derivation failed");
  }

  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    OPENSSL_cleanse(key, sizeof(key));
    if (!pass_copy.empty()) {
      OPENSSL_cleanse(&pass_copy[0], pass_copy.size());
      pass_copy.clear();
    }
    throw std::runtime_error("EVP_CIPHER_CTX_new failed");
  }

  if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(key, sizeof(key));
    if (!pass_copy.empty()) {
      OPENSSL_cleanse(&pass_copy[0], pass_copy.size());
      pass_copy.clear();
    }
    throw std::runtime_error("EncryptInit failed");
  }
  if (EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(key, sizeof(key));
    if (!pass_copy.empty()) {
      OPENSSL_cleanse(&pass_copy[0], pass_copy.size());
      pass_copy.clear();
    }
    throw std::runtime_error("EncryptInit set key/iv failed");
  }

  std::vector<unsigned char> out(texto.size() + 16);
  int out_len1 = 0;
  if (EVP_EncryptUpdate(ctx, out.data(), &out_len1, (const unsigned char *)texto.data(), (int)texto.size()) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(key, sizeof(key));
    if (!pass_copy.empty()) {
      OPENSSL_cleanse(&pass_copy[0], pass_copy.size());
      pass_copy.clear();
    }
    if (!texto.empty()) {
      OPENSSL_cleanse(&texto[0], texto.size());
      texto.clear();
    }
    throw std::runtime_error("EncryptUpdate failed");
  }

  int out_len2 = 0;
  if (EVP_EncryptFinal_ex(ctx, out.data() + out_len1, &out_len2) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(key, sizeof(key));
    if (!pass_copy.empty()) {
      OPENSSL_cleanse(&pass_copy[0], pass_copy.size());
      pass_copy.clear();
    }
    if (!texto.empty()) {
      OPENSSL_cleanse(&texto[0], texto.size());
      texto.clear();
    }
    throw std::runtime_error("EncryptFinal failed");
  }

  int cipher_len = out_len1 + out_len2;
  out.resize(cipher_len);

  unsigned char tag[16];
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, sizeof(tag), tag) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(key, sizeof(key));
    if (!pass_copy.empty()) {
      OPENSSL_cleanse(&pass_copy[0], pass_copy.size());
      pass_copy.clear();
    }
    if (!texto.empty()) {
      OPENSSL_cleanse(&texto[0], texto.size());
      texto.clear();
    }
    throw std::runtime_error("Get GCM tag failed");
  }

  EVP_CIPHER_CTX_free(ctx);

  std::vector<unsigned char> blob;
  blob.insert(blob.end(), salt, salt + sizeof(salt));
  blob.insert(blob.end(), iv, iv + sizeof(iv));
  blob.insert(blob.end(), tag, tag + sizeof(tag));
  blob.insert(blob.end(), out.begin(), out.end());

  // Limpiar todo lo sensible EN ORDEN CORRECTO
  OPENSSL_cleanse(key, sizeof(key));
  if (!pass_copy.empty()) {
    OPENSSL_cleanse(&pass_copy[0], pass_copy.size());
    pass_copy.clear();
  }
  if (!texto.empty()) {
    OPENSSL_cleanse(&texto[0], texto.size());
    texto.clear();
  }
  if (!out.empty()) {
    OPENSSL_cleanse(out.data(), (int)out.size());
    out.clear();
  }

  return b64_encode(blob.data(), (int)blob.size());
}

std::string Cifrado::desencriptar(const std::string &b64, const std::string &pass) {
  std::string pass_copy = pass;

  auto blob = b64_decode(b64);
  if (blob.size() < 16 + 12 + 16) {
    if (!pass_copy.empty()) {
      OPENSSL_cleanse(&pass_copy[0], pass_copy.size());
      pass_copy.clear();
    }
    return "";
  }

  unsigned char *salt = blob.data();
  unsigned char *iv = blob.data() + 16;
  unsigned char *tag = blob.data() + 16 + 12;
  unsigned char *ct = blob.data() + 16 + 12 + 16;
  int ct_len = (int)(blob.size() - (16 + 12 + 16));

  unsigned char key[32];
  // USANDO MISMO NUMERO DE ITERACIONES QUE ENCRYPT
  if (!PKCS5_PBKDF2_HMAC(pass_copy.c_str(), (int)pass_copy.size(), salt, 16, ITERACIONES_PBKDF2, EVP_sha256(), sizeof(key), key)) {
    if (!pass_copy.empty()) {
      OPENSSL_cleanse(&pass_copy[0], pass_copy.size());
      pass_copy.clear();
    }
    if (!blob.empty()) {
      OPENSSL_cleanse(blob.data(), (int)blob.size());
      blob.clear();
    }
    throw std::runtime_error("PBKDF2 key derivation failed");
  }

  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  if (!ctx) {
    OPENSSL_cleanse(key, sizeof(key));
    if (!pass_copy.empty()) {
      OPENSSL_cleanse(&pass_copy[0], pass_copy.size());
      pass_copy.clear();
    }
    if (!blob.empty()) {
      OPENSSL_cleanse(blob.data(), (int)blob.size());
      blob.clear();
    }
    throw std::runtime_error("EVP_CIPHER_CTX_new failed");
  }

  if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(key, sizeof(key));
    if (!pass_copy.empty()) {
      OPENSSL_cleanse(&pass_copy[0], pass_copy.size());
      pass_copy.clear();
    }
    if (!blob.empty()) {
      OPENSSL_cleanse(blob.data(), (int)blob.size());
      blob.clear();
    }
    throw std::runtime_error("DecryptInit failed");
  }
  if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(key, sizeof(key));
    if (!pass_copy.empty()) {
      OPENSSL_cleanse(&pass_copy[0], pass_copy.size());
      pass_copy.clear();
    }
    if (!blob.empty()) {
      OPENSSL_cleanse(blob.data(), (int)blob.size());
      blob.clear();
    }
    throw std::runtime_error("DecryptInit set key/iv failed");
  }

  std::vector<unsigned char> out(ct_len + 16);
  int out_len1 = 0;
  if (EVP_DecryptUpdate(ctx, out.data(), &out_len1, ct, ct_len) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(key, sizeof(key));
    if (!pass_copy.empty()) {
      OPENSSL_cleanse(&pass_copy[0], pass_copy.size());
      pass_copy.clear();
    }
    if (!blob.empty()) {
      OPENSSL_cleanse(blob.data(), (int)blob.size());
      blob.clear();
    }
    if (!out.empty()) {
      OPENSSL_cleanse(out.data(), (int)out.size());
      out.clear();
    }
    throw std::runtime_error("DecryptUpdate failed");
  }

  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(key, sizeof(key));
    if (!pass_copy.empty()) {
      OPENSSL_cleanse(&pass_copy[0], pass_copy.size());
      pass_copy.clear();
    }
    if (!blob.empty()) {
      OPENSSL_cleanse(blob.data(), (int)blob.size());
      blob.clear();
    }
    if (!out.empty()) {
      OPENSSL_cleanse(out.data(), (int)out.size());
      out.clear();
    }
    throw std::runtime_error("Set GCM tag failed");
  }

  int out_len2 = 0;
  int final_rc = EVP_DecryptFinal_ex(ctx, out.data() + out_len1, &out_len2);
  EVP_CIPHER_CTX_free(ctx);

  if (final_rc != 1) {
    OPENSSL_cleanse(key, sizeof(key));
    if (!pass_copy.empty()) {
      OPENSSL_cleanse(&pass_copy[0], pass_copy.size());
      pass_copy.clear();
    }
    if (!blob.empty()) {
      OPENSSL_cleanse(blob.data(), (int)blob.size());
      blob.clear();
    }
    if (!out.empty()) {
      OPENSSL_cleanse(out.data(), (int)out.size());
      out.clear();
    }
    return "";
  }

  out.resize(out_len1 + out_len2);
  std::string resultado(out.begin(), out.end());

  // Limpiar datos temporales SENSIBLES PERO NO EL RESULTADO FINAL
  OPENSSL_cleanse(key, sizeof(key));
  if (!pass_copy.empty()) {
    OPENSSL_cleanse(&pass_copy[0], pass_copy.size());
    pass_copy.clear();
  }
  if (!blob.empty()) {
    OPENSSL_cleanse(blob.data(), (int)blob.size());
    blob.clear();
  }
  if (!out.empty()) {
    OPENSSL_cleanse(out.data(), (int)out.size());
    out.clear();
  }

  return resultado;
}
