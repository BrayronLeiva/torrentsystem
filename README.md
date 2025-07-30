# torrentsystem
This is a torrent system implemented in C
Description # Sistema de Torrent Distribuido en C

Este proyecto implementa un sistema distribuido de transferencia de archivos inspirado en el modelo **BitTorrent**, desarrollado completamente en **C** con **sockets TCP** y soporte para múltiples conexiones concurrentes.  

El sistema consta de dos programas principales:
- **`catalogarizador`**: Actúa como **tracker** y coordina la red.
- **`catalogarizadorClient.c`**: Actúa como **peer** que puede enviar y recibir archivos directamente con otros peers.

---

## 🔧 Funcionamiento general

1. **Tracker (`catalogarizador.c`)**
   - Se ejecuta primero y funciona como punto central de coordinación.
   - Cuando un peer se conecta:
     1. El peer envía su archivo binario con la lista de archivos que posee.
     2. El tracker actualiza la lista global y hace un *broadcast* de esta lista actualizada a todos los peers conectados.
   - Gracias a esta lista compartida, cada peer sabe **qué archivos tiene cada quién**, y puede dividir la carga de red solicitando partes de un archivo a múltiples peers en paralelo.

2. **Peers (`catalogarizadorClient.c`)**
   - Se conectan al tracker y reciben la lista global de archivos.
   - Usan el archivo binario actualizado para saber:
     - Qué peers tienen el archivo que necesitan.
     - Cómo dividir la descarga en fragmentos (partes 1, 2, 3, etc.).
   - Pueden solicitar simultáneamente distintos fragmentos del mismo archivo a varios peers, reconstruyendo el archivo final localmente.
   - También pueden enviar archivos: cuando reciben una solicitud, dividen el archivo en partes y envían solo la parte asignada.

---

## 📂 Archivo binario de metadatos

Cada vez que un peer se conecta, envía un archivo binario con información de sus archivos.  
Este archivo contiene por cada archivo compartido:

- **Nombre del archivo** (sin ruta completa).
- **Ruta completa del archivo**.
- **Tamaño en bytes**.
- **Hash calculado del contenido**.
- **IP y puerto** (incluidos en el nombre del archivo como referencia).

El tracker propaga este archivo binario actualizado a todos los peers conectados en forma de broadcast, asegurando que todos tengan siempre la misma información.

---

## 🔄 Flujo de operación

1️⃣ Ejecutar `catalogarizador.c` en la máquina que actuará como tracker.  
2️⃣ Ejecutar `catalogarizadorClient.c` en tantas máquinas como peers se deseen.  
3️⃣ Cada peer selecciona un directorio al iniciar, escaneando recursivamente todos los archivos para generar su archivo binario con metadatos.  
4️⃣ Cuando un peer quiere descargar un archivo:
   - Consulta el archivo binario para ver qué peers lo tienen.
   - Lanza **hilos concurrentes** para pedir diferentes partes del archivo a distintos peers.
   - Reconstruye el archivo original ensamblando los fragmentos recibidos.
5️⃣ Si un peer envía un archivo:
   - Divide el archivo en `n` partes y envía solo la parte solicitada por cada peer.

---

## 📡 Comunicación en red

- **Protocolo**: TCP (sockets).
- **Notificación de nuevos peers**: El tracker envía el archivo binario actualizado a todos los peers mediante broadcast cuando alguien se conecta.
- **Transferencia de archivos**: P2P directo entre peers, sin pasar por el tracker.

---

## 🖥️ Ejemplo de flujo

sequenceDiagram
    participant PeerA
    participant Tracker
    participant PeerB
    participant PeerC

    PeerA->>Tracker: Envía su archivo binario con metadatos.
    Tracker->>PeerB: Broadcast archivo binario actualizado.
    Tracker->>PeerC: Broadcast archivo binario actualizado.
    PeerB->>PeerA: Solicita fragmento 1 de archivo X.
    PeerC->>PeerA: Solicita fragmento 2 de archivo X.
    PeerB->>PeerC: Solicita fragmento 3 de archivo X.
    PeerB+PeerC->>PeerA: Envían fragmentos.
    PeerB->>PeerB: Reconstruye archivo localmente.
