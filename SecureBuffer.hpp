/*
 * SecureBuffer.hpp
 *
 *  Created on: 30 jun 2026
 *      Author: DjSteker
 */

#ifndef SECUREBUFFER_HPP_
#define SECUREBUFFER_HPP_

#include <string>
#include <openssl/crypto.h>
#include <utility>

class SecureString {
private:
  std::string m_data;

public:
  SecureString() = default;
  explicit SecureString(const std::string& str) : m_data(str) {}
  explicit SecureString(const char* str) : m_data(str ? str : "") {}

  ~SecureString() {
    if (!m_data.empty()) {
      OPENSSL_cleanse(&m_data[0], m_data.size());
    }
    m_data.clear();
    m_data.shrink_to_fit();
  }

  // Move constructor/assignment only (no copy!)
  SecureString(SecureString&& other) noexcept : m_data(std::move(other.m_data)) {}

  SecureString& operator=(SecureString&& other) noexcept {
    if (this != &other) {
      if (!m_data.empty()) {
        OPENSSL_cleanse(&m_data[0], m_data.size());
      }
      m_data = std::move(other.m_data);
    }
    return *this;
  }

  // Delete copy operations
  SecureString(const SecureString&) = delete;
  SecureString& operator=(const SecureString&) = delete;

  // Accessors
  const std::string& get() const {
    return m_data;
  }
  bool empty() const {
    return m_data.empty();
  }
  size_t size() const {
    return m_data.size();
  }

  // Implicit conversion to const char* for convenience
  operator const std::string&() const {
    return m_data;
  }
};

#endif /* SECUREBUFFER_HPP_ */
