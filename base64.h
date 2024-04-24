//
// Created by admin on 2024/4/24.
//

#ifndef HTTPS_TUNNEL_CLIENT_BASE64_H
#define HTTPS_TUNNEL_CLIENT_BASE64_H

#endif //HTTPS_TUNNEL_CLIENT_BASE64_H
char *base64_encode(const unsigned char *input, size_t len);
unsigned char *base64_decode(const char *input, size_t len, size_t *out_len);