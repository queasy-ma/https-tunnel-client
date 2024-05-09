//
// Created by admin on 2024/5/9.
//

#ifndef HTTPS_TUNNEL_CLIENT_DNS_H
#define HTTPS_TUNNEL_CLIENT_DNS_H
#ifdef __cplusplus
extern "C"
{
#endif

int gethostbydomain(const char *domain, char *ipAddrBuf, int size);

#ifdef __cplusplus
}
#endif
#endif //HTTPS_TUNNEL_CLIENT_DNS_H
