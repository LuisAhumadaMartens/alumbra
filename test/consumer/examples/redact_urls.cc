#include <string>

#include "umbra/base/url_redactor.h"

int main() {
  const umbra::base::UrlRedactor redactor;
  const std::string url = redactor.RedactUrl(
      "https://user:secret@cdn.example.com:8443/v/main.m3u8?token=abc#t=10");
  const std::string header =
      redactor.RedactField("Authorization", "Bearer abc");
  const std::string message = redactor.RedactText(
      "request to https://cdn.example.com/v/seg-1.m4s?sig=abc timed out");

  const bool ok = url.starts_with("https://cdn.example.com:8443 [url:") &&
                  url.find("secret") == std::string::npos &&
                  url.find("main.m3u8") == std::string::npos &&
                  header == "[redacted]" &&
                  message.starts_with("request to https://cdn.example.com ") &&
                  message.find("sig=abc") == std::string::npos;
  return ok ? 0 : 1;
}
