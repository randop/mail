#pragma once

static constexpr size_t DEFAULT_EMAIL_SIZE_LIMIT = 524288;     // 512KB
static constexpr size_t SMTP_COMMAND_BUFFER_SIZE_LIMIT = 2500; // 2KB

enum class SMTP_COMMAND : uint8_t {
  AUTH,
  DATA,
  EHLO,
  EXPN,
  HELO,
  HELP,
  MAIL,
  NOOP,
  QUIT,
  RCPT,
  RSET,
  STARTTLS,
  VRFY,
  UNKNOWN,
};