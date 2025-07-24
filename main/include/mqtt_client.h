#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include "hv/hv.h"
#include "hv/mqtt_client.h"
#include "hv/hmutex.h"

// Estructura wrapper para reemplazar condition variables
typedef struct {
    hmutex_t mutex;
    int signal_flag;
} cond_wrapper_t;

// Declaración adelantada opaca
typedef struct MqttContext MqttContext;

// Funciones públicas
MqttContext* mqtt_context_create(void);
void mqtt_context_free(MqttContext* ctx); // AHORA LA IMPLEMENTAMOS
int mqtt_configure_tls(MqttContext* ctx, const char* ca_file, const char* client_cert, const char* client_key, int verify_peer);
int mqtt_connect(MqttContext* ctx, const char* host, int port, const char* client_id, int ssl_enabled, int timeout_ms);
int mqtt_publish(MqttContext* ctx, const char* topic, const char* payload, int qos, int timeout_ms);
void mqtt_disconnect(MqttContext* ctx);
hloop_t* mqtt_context_get_loop(MqttContext* ctx);
#endif // MQTT_CLIENT_H
