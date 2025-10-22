---
description: Industria4.0 es un sistema de visión artificial que identifica objetos en tiempo real y envía alarmas por MQTT a AWS (cuando hay internet) o por LoRaWAN en ausencia de conectividad.
title: Industria4.0 - Real-time Object Detection with MQTT & LoRaWAN Alerts
keywords:
  - Industria4.0
  - reCamera
  - Object detection
  - YOLO
  - MQTT
  - LoRaWAN
  - AWS
  - C++
image: https://files.seeedstudio.com/wiki/wiki-platform/S-tempor.png
slug: /industria4.0_object_detection_mqtt_lorawan
last_update:
  date: 09/11/2025
  author: Victoria Galarza

no_comments: false
---

# Industria4.0 - Real-time Object Detection with MQTT & LoRaWAN Alerts

Industria4.0 es un sistema de visión artificial basado en **YOLOv11** y **reCamera**, desarrollado en C++.  
El proyecto permite **identificar objetos en tiempo real** y enviar alarmas de dos formas:  
- **MQTT hacia AWS IoT Core** cuando existe conexión a internet.  
- **LoRaWAN** en caso de no contar con internet.  

De esta manera, el sistema garantiza la transmisión de alertas críticas en entornos industriales y de monitoreo remoto, incluso en condiciones de conectividad limitada.  

## Environment Preparation
### Pre-compilation

Primero, configure el entorno de compilación cruzada en Linux siguiendo la guía de **Develop with C/C++**.  
**Nota**: Reconfigure la ruta después de cada reinicio:

```bash
export SG200X_SDK_PATH=$HOME/recamera/sg2002_recamera_emmc/
export PATH=$HOME/recamera/host-tools/gcc/riscv64-linux-musl-x86_64/bin:$PATH

