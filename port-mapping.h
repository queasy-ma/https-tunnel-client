//
// Created by admin on 2024/5/15.
//

#ifndef HTTPS_TUNNEL_CLIENT_PORT_MAPPING_H
#define HTTPS_TUNNEL_CLIENT_PORT_MAPPING_H
#include "rest_client_pool.h"

void PORTMAP(char *uuid, CurlPool *pool, char *tartget_ip, unsigned short target_port);

#endif //HTTPS_TUNNEL_CLIENT_PORT_MAPPING_H
