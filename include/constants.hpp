#pragma once

#include <array>
#include <seastar/core/sstring.hh>
#include <string_view>

static constexpr size_t DEFAULT_EMAIL_SIZE_LIMIT = 524288;     // 512KB
static constexpr size_t SMTP_COMMAND_BUFFER_SIZE_LIMIT = 2500; // 2KB

constexpr const char SMTP_CRLF_RAW[] = "\r\n";
constexpr std::string_view SMTP_CRLF{SMTP_CRLF_RAW, 2};

constexpr const char SMTP_DATA_END_RAW[] = "\r\n.\r\n";
constexpr std::string_view SMTP_DATA_END{SMTP_DATA_END_RAW, 5};

constexpr const char SMTP_UNKNOWN_RAW[] = "UNKNOWN";
constexpr std::string_view SMTP_UNKNOWN{SMTP_UNKNOWN_RAW, 7};

enum class SMTP_COMMAND : uint8_t {
  HELO,
  EHLO,
  MAIL,
  RCPT,
  DATA,
  RSET,
  QUIT,
  NOOP,
  VRFY,
  EXPN,
  HELP,
  AUTH,
  STARTTLS,
  BDAT,
  SEND,
  SOML,
  UNKNOWN
};

// All standard SMTP commands (RFC 5321 + common extensions)
// clang-format off
/***
| Command     | Client Example                                      | Purpose |
|-------------|-----------------------------------------------------|-------|
| **HELO**    | `HELO client.example.org`                           | Old-style greeting |
| **EHLO**    | `EHLO client.example.org`                           | Modern greeting + discover extensions |
| **MAIL**    | `MAIL FROM:<user@example.org> SIZE=1024`            | Start mail transaction (sender) |
| **RCPT**    | `RCPT TO:<recipient@domain.com>`                    | Specify recipient(s) |
| **DATA**    | `DATA`                                              | Begin message content |
| **RSET**    | `RSET`                                              | Reset current transaction |
| **QUIT**    | `QUIT`                                              | End session |
| **NOOP**    | `NOOP`                                              | Do nothing (keep-alive) |
| **VRFY**    | `VRFY john.doe@example.com`                         | Verify if address exists |
| **EXPN**    | `EXPN mailing-list`                                 | Expand mailing list |
| **HELP**    | `HELP` or `HELP MAIL`                               | Get help |
| **AUTH**    | `AUTH LOGIN` or `AUTH PLAIN`                        | Authentication |
| **STARTTLS**| `STARTTLS`                                          | Upgrade to TLS |
| **BDAT**    | `BDAT 1024 LAST`                                    | Send binary chunk (CHUNKING extension) |
| **SEND**    | `SEND FROM:<user@example.org>`                      | Deprecated (send to terminal) |
| **SOML**    | `SOML FROM:<user@example.org>`                      | Deprecated (Send or Mail) |
***/
/*** IMPORTANT: Padded spaces exist for smtp parser exact matching ***/
// clang-format on
constexpr std::array<std::pair<std::string_view, SMTP_COMMAND>, 16>
    SMTP_RFC_COMMANDS = {{{"HELO ", SMTP_COMMAND::HELO},
                          {"EHLO ", SMTP_COMMAND::EHLO},
                          {"MAIL FROM:", SMTP_COMMAND::MAIL},
                          {"RCPT TO:", SMTP_COMMAND::RCPT},
                          {"DATA", SMTP_COMMAND::DATA},
                          {"RSET", SMTP_COMMAND::RSET},
                          {"QUIT", SMTP_COMMAND::QUIT},
                          {"NOOP", SMTP_COMMAND::NOOP},
                          {"VRFY ", SMTP_COMMAND::VRFY},
                          {"EXPN ", SMTP_COMMAND::EXPN},
                          {"HELP", SMTP_COMMAND::HELP},
                          {"AUTH ", SMTP_COMMAND::AUTH},
                          {"STARTTLS", SMTP_COMMAND::STARTTLS},
                          {"BDAT ", SMTP_COMMAND::BDAT},
                          {"SEND ", SMTP_COMMAND::SEND},
                          {"SOML ", SMTP_COMMAND::SOML}}};

enum class SMTP_SESSION_STATUS : uint8_t {
  UNKNOWN,
  COMMAND,
  DATA,
  ERROR,
  PROXY
};