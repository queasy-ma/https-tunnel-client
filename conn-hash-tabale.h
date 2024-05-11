
#ifndef HTTPS_TUNNEL_CLIENT_CONN_HASH_TABALE_H
#define HTTPS_TUNNEL_CLIENT_CONN_HASH_TABALE_H
#include <stdbool.h>
bool initializeTable();
bool addConnection(const char *uuid, int sock);
bool findConnection(const char *uuid);
bool deleteConnection(const char *uuid);
bool changeConnection(const char *uuid, int newValue);
#endif //HTTPS_TUNNEL_CLIENT_CONN_HASH_TABALE_H
