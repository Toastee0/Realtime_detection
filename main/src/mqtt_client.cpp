#include "mqtt_client.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct MqttContext {
    mqtt_client_t* client;
    hloop_t* loop;
    cond_wrapper_t cond_wrapper;
    int connected;
    int published;
    int publish_result;
};

// -------- Helpers

static void cond_signal(cond_wrapper_t* cond) {
    hmutex_lock(&cond->mutex);
    cond->signal_flag = 1;
    hmutex_unlock(&cond->mutex);
}
hloop_t* mqtt_context_get_loop(MqttContext* ctx) {
    return ctx->loop;
}
static int cond_timedwait(cond_wrapper_t* cond, hmutex_t* mutex, const struct timespec* ts) {
    hmutex_lock(&cond->mutex);
    time_t start = time(NULL);
    while (!cond->signal_flag) {
        hmutex_unlock(&cond->mutex);
        usleep(10 * 1000); // 10ms
        if (ts && (time(NULL) - start) * 1000 >= (ts->tv_sec * 1000 + ts->tv_nsec / 1000000)) {
            return -1; // Timeout
        }
        hmutex_lock(&cond->mutex);
    }
    cond->signal_flag = 0;
    hmutex_unlock(&cond->mutex);
    return 0;
}

// -------- Callback

static void mqtt_event_handler(mqtt_client_t* client, int event_type) {
    MqttContext* ctx = (MqttContext*)mqtt_client_get_userdata(client);
    if (!ctx) return;

    hmutex_lock(&ctx->cond_wrapper.mutex);

    switch (event_type) {
        case MQTT_TYPE_CONNACK:
            ctx->connected = 1;
            cond_signal(&ctx->cond_wrapper);
            break;
        case MQTT_TYPE_PUBACK:
            ctx->published = 1;
            ctx->publish_result = client->mid;
            cond_signal(&ctx->cond_wrapper);
            break;
        case MQTT_TYPE_DISCONNECT:
            ctx->connected = 0;
            cond_signal(&ctx->cond_wrapper);
            break;
        default:
            break;
    }

    hmutex_unlock(&ctx->cond_wrapper.mutex);
}

// -------- Crear / Liberar

MqttContext* mqtt_context_create() {
    MqttContext* ctx = (MqttContext*)malloc(sizeof(MqttContext));
    if (!ctx) return NULL;

    memset(ctx, 0, sizeof(MqttContext));
    ctx->loop = hloop_new(0);
    ctx->client = mqtt_client_new(ctx->loop);

    if (!ctx->client) {
        hloop_free(&ctx->loop);
        free(ctx);
        return NULL;
    }

    hmutex_init(&ctx->cond_wrapper.mutex);
    ctx->cond_wrapper.signal_flag = 0;
    return ctx;
}

void mqtt_context_free(MqttContext* ctx) {
    if (ctx) {
        if (ctx->client) {
            mqtt_client_free(ctx->client);  // ✅ CORREGIDO
            ctx->client = NULL;
        }
        free(ctx);
    }
}

// -------- TLS

int mqtt_configure_tls(MqttContext* ctx, const char* ca_file, const char* client_cert, const char* client_key, int verify_peer) {
    if (!ctx || !ctx->client) return -1;

    hssl_ctx_opt_t ssl_opt;
    memset(&ssl_opt, 0, sizeof(ssl_opt));

    ssl_opt.verify_peer = verify_peer;
    ssl_opt.ca_file = ca_file;
    ssl_opt.crt_file = client_cert;
    ssl_opt.key_file = client_key;

    return mqtt_client_new_ssl_ctx(ctx->client, &ssl_opt);
}

// -------- Conexión

int mqtt_connect(MqttContext* ctx, const char* host, int port, const char* client_id, int ssl_enabled, int timeout_ms) {
    if (!ctx || !ctx->client) {
        fprintf(stderr, "E MQTT context no válido\n");
        return -1;
    }

    mqtt_client_set_callback(ctx->client, mqtt_event_handler);
    mqtt_client_set_userdata(ctx->client, ctx);

    if (client_id) {
        mqtt_client_set_id(ctx->client, client_id);
    }
    if (access("/etc/ssl/certs/cam_1/AmazonRootCA1.pem", F_OK) == 0 &&
        access("/etc/ssl/certs/cam_1/cam_1-private.pem.key", F_OK) == 0 &&
        access("/etc/ssl/certs/cam_1/cam_1-certificate.pem.crt", F_OK) == 0) {
        fprintf(stdout, "I Todos los certificados encontrados\n");
    } else {
        fprintf(stderr, "E Faltan uno o más certificados\n");
    }
    mqtt_client_set_connect_timeout(ctx->client, timeout_ms);

    int max_retries = 30;
    int attempt = 0;
    int result = -1;

    while (attempt < max_retries) {
        attempt++;
        fprintf(stdout, "Intentando conectar a MQTT (%d/%d)...\n", attempt, max_retries);
        result = mqtt_client_connect(ctx->client, host, port, ssl_enabled);

        if (result == 0) {
            fprintf(stdout, "Conexión MQTT exitosa\n");
            break;
        } else {
            fprintf(stdout, "Conectando a MQTT broker en host: %s, puerto: %d, SSL: %d\n", host, port, ssl_enabled);

            fprintf(stderr, "E Fallo al conectar MQTT (código %d)\n", result);
            sleep(1); // Espera entre intentos
        }
    }

    if (result != 0) {
        fprintf(stderr, "E No se pudo establecer conexión MQTT después de %d intentos\n", max_retries);
        return result;
    }

    return 0;
}

// -------- Publicar

int mqtt_publish(MqttContext* ctx, const char* topic, const char* payload, int qos, int timeout_ms) {
    // Verificaciones básicas
    if (!ctx || !ctx->client) {
        printf("Error: Contexto MQTT inválido\n");
        return -1;
    }
    if (!topic || !payload) {
        printf("Error: Topic o payload nulo\n");
        return -1;
    }

    printf("Publicando en topic: %s\n", topic);
    printf("Payload: %.*s\n", (int)strlen(payload) > 100 ? 100 : (int)strlen(payload), payload);

    mqtt_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.topic = topic;
    msg.payload = payload;
    msg.payload_len = strlen(payload);
    msg.qos = qos;

    // Intenta publicar
    int result = mqtt_client_publish(ctx->client, &msg);
    
    if (result < 0) {
        printf("Error al publicar (%d). Intentando recuperación...\n", result);
        
        // 1. Desconectar
        mqtt_client_disconnect(ctx->client);
        
        // 2. Reconectar (usa los valores de tu configuración original)
        const char* aws_endpoint = "a265m6rkc34opn-ats.iot.us-east-1.amazonaws.com";
        const char* client_id = "cam_1";
        if (mqtt_connect(ctx, aws_endpoint, 8883, client_id, 1, 5000) != 0) {
            printf("Error al reconectar\n");
            return -1;
        }
        
        // 3. Reintentar publicación
        result = mqtt_client_publish(ctx->client, &msg);
    }

    return result;
}
void mqtt_disconnect(MqttContext* ctx) {
    if (ctx && ctx->client) {
        mqtt_client_disconnect(ctx->client);
    }
}
