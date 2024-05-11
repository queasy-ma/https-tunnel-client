#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include "conn-hash-tabale.h"

#define INITIAL_TABLE_SIZE 100
#define MAX_LOAD_FACTOR 0.75

typedef struct kvNode {
    char *key;
    int value;
    struct kvNode *next;
} kvNode;

kvNode **hashTable;
int tableSize = INITIAL_TABLE_SIZE;
int numItems = 0;
pthread_mutex_t lock;

// Combined hash function with switch-case for selecting hash type
unsigned int hash(const char *key, int type) {
    unsigned long int value = 0;
    int i = 0;

    switch (type) {
        case 1: // First hash function
            for (i = 0; key[i]; i++) {
                value = value * 37 + key[i];
            }
            break;
        case 2: // Second hash function
            for (i = 0; key[i]; i++) {
                value = value * 41 + key[i];
            }
            break;
        case 3: // Third hash function
            for (i = 0; key[i]; i++) {
                value = value * 31 + key[i];
            }
            break;
        default:
            return 0; // Invalid hash type
    }
    return value % tableSize;
}

bool initializeTable() {
    hashTable = malloc(sizeof(kvNode*) * tableSize);
    if (!hashTable) return false;
    for (int i = 0; i < tableSize; i++) {
        hashTable[i] = NULL;
    }
    return pthread_mutex_init(&lock, NULL) == 0;
}

bool addConnection(const char *uuid, int sock) {
    if ((double)numItems / tableSize > MAX_LOAD_FACTOR) {
        // Placeholder for potential rehashing code if needed
        // rehash();
    }

    pthread_mutex_lock(&lock);

    unsigned int index;
    kvNode *node;
    for (int type = 1; type <= 3; type++) {
        index = hash(uuid, type);
        node = hashTable[index];

        bool exists = false;
        while (node) {
            if (strcmp(node->key, uuid) == 0) {
                exists = true;
                break;
            }
            node = node->next;
        }

        if (!exists && !hashTable[index]) { // Check if slot is empty and key does not exist
            kvNode *newNode = malloc(sizeof(kvNode));
            if (!newNode) {
                pthread_mutex_unlock(&lock);
                return false;
            }
            newNode->key = strdup(uuid);
            newNode->value = sock;
            newNode->next = hashTable[index];
            hashTable[index] = newNode;
            numItems++;
            pthread_mutex_unlock(&lock);
            return true;
        }
    }

    pthread_mutex_unlock(&lock);
    return false; // All hash functions tried and no space found
}

bool findConnection(const char *uuid) {
    pthread_mutex_lock(&lock);

    for (int type = 1; type <= 3; type++) {
        unsigned int index = hash(uuid, type);
        kvNode *node = hashTable[index];
        while (node) {
            if (strcmp(node->key, uuid) == 0) {
                pthread_mutex_unlock(&lock);
                return true;
            }
            node = node->next;
        }
    }

    pthread_mutex_unlock(&lock);
    return false;
}

bool deleteConnection(const char *uuid) {
    pthread_mutex_lock(&lock);  // 加锁以保证线程安全

    bool found = false;
    for (int type = 1; type <= 3; type++) {  // 尝试所有的哈希函数
        unsigned int index = hash(uuid, type);
        kvNode **node = &hashTable[index];

        while (*node) {
            if (strcmp((*node)->key, uuid) == 0) {
                kvNode *temp = *node;
                *node = temp->next;  // 从链表中移除元素
                free(temp->key);  // 释放字符串内存
                free(temp);       // 释放节点内存
                numItems--;       // 更新存储的元素数量
                found = true;
                break;  // 删除成功后退出循环
            }
            node = &(*node)->next;  // 移动到下一个节点
        }

        if (found) {
            break;  // 如果已找到并删除了元素，则不需继续尝试其他哈希函数
        }
    }

    pthread_mutex_unlock(&lock);  // 解锁
    return found;  // 返回是否成功删除元素
}

bool changeConnection(const char *uuid, int newValue) {
    pthread_mutex_lock(&lock);  // 加锁以保证线程安全

    bool found = false;
    for (int type = 1; type <= 3; type++) {  // 尝试所有的哈希函数
        unsigned int index = hash(uuid, type);
        kvNode *node = hashTable[index];

        while (node) {
            if (strcmp(node->key, uuid) == 0) {
                node->value = newValue;  // 更新节点的值
                found = true;
                break;  // 更新成功后退出循环
            }
            node = node->next;  // 移动到下一个节点
        }

        if (found) {
            break;  // 如果已找到并更新了元素，则不需继续尝试其他哈希函数
        }
    }

    pthread_mutex_unlock(&lock);  // 解锁
    return found;  // 返回是否成功更新元素
}
