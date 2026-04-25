#include "x509_helpers.hpp"

namespace x509_helpers {
std::errc map_errno(int e) noexcept {
  switch (e) {
  case ENOENT:
    return std::errc::no_such_file_or_directory;
  case EACCES:
    return std::errc::permission_denied;
  case ENOTDIR:
    return std::errc::not_a_directory;
  default:
    return std::errc::io_error;
  }
}

std::string extract_common_name(X509 *cert) {
  X509_NAME *subject = X509_get_subject_name(cert);
  if (!subject) {
    return {};
  }

  int idx = X509_NAME_get_index_by_NID(subject, NID_commonName, -1);
  if (idx < 0) {
    return {};
  }

  X509_NAME_ENTRY *entry = X509_NAME_get_entry(subject, idx);
  ASN1_STRING *data = X509_NAME_ENTRY_get_data(entry);

  const unsigned char *raw = ASN1_STRING_get0_data(data);
  int len = ASN1_STRING_length(data);

  if (!raw || len <= 0) {
    return {};
  }

  return std::string(reinterpret_cast<const char *>(raw),
                     static_cast<size_t>(len));
}

x509_parse_result parse_x509(const std::string &file_path) {
  FILE *fp = std::fopen(file_path.c_str(), "rb");
  if (!fp) {
    return {"", map_errno(errno)};
  }

  X509 *cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
  std::fclose(fp);

  if (!cert) {
    return {"", std::errc::invalid_argument};
  }

  std::string cn = extract_common_name(cert);
  X509_free(cert);

  if (cn.empty()) {
    return {"", std::errc::result_out_of_range};
  }

  return {std::move(cn), {}};
}

std::errc check_private_key(const std::string &file_path) {
  FILE *fp = std::fopen(file_path.c_str(), "rb");
  if (!fp) {
    return map_errno(errno);
  }

  EVP_PKEY *pkey = PEM_read_PrivateKey(fp, nullptr, nullptr, nullptr);
  std::fclose(fp);

  if (!pkey) {
    return std::errc::invalid_argument;
  }

  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(pkey, nullptr);
  if (!ctx) {
    EVP_PKEY_free(pkey);
    return std::errc::io_error;
  }

  int ok = EVP_PKEY_check(ctx);

  EVP_PKEY_CTX_free(ctx);
  EVP_PKEY_free(pkey);

  if (ok == 1) {
    return {};
  }
  if (ok == 0) {
    return std::errc::invalid_argument;
  }
  return std::errc::io_error;
}

} // namespace x509_helpers