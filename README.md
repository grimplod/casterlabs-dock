# Casterlabs Caffeinated en OBS

Plugin nativo para Windows x64, probado con OBS Studio 33.0.1 y Casterlabs Caffeinated. Acopla la ventana principal de Casterlabs para que puedas usar su panel de control dentro de OBS con la sesión que ya tienes iniciada.

## Uso

1. Abre Casterlabs Caffeinated y OBS con los mismos permisos de Windows. Si Casterlabs está en la bandeja, muestra su ventana principal.
2. En OBS, abre **Paneles → Casterlabs Caffeinated**. Si la ventana de Casterlabs ya está abierta, se acopla automáticamente. También puedes pulsar **Abrir o acoplar Casterlabs**.
3. Para volver a la ventana independiente, pulsa **Desacoplar Casterlabs** o cierra el panel. Puedes mover el panel a otra zona de OBS o dejarlo flotante.

Casterlabs se ejecuta como aplicación de Windows mientras usas el panel. Para que la interfaz tenga espacio suficiente, el panel reserva al menos 400 × 300 píxeles lógicos para la ventana.

## Instalar en otro equipo

1. Cierra OBS y extrae el ZIP.
2. Copia la carpeta `plugins\casterlabs-dock` a `C:\ProgramData\obs-studio\plugins\`. Windows puede pedir permisos de administrador. Para OBS portátil, copia `plugins\casterlabs-dock` dentro de la carpeta `plugins` de la instalación portátil.
3. Abre OBS y busca **Casterlabs Caffeinated** en el menú **Paneles**.

Para desinstalar, cierra OBS y elimina la carpeta `casterlabs-dock` que copiaste.

## Compilar

El código está en `fuente`. `Build.ps1` usa PowerShell 7, Zig 0.15.2, las cabeceras de OBS 33.0.1, los paquetes de Qt 6.11.1 de OBS (`obs-deps`, versión `2026-10-23`) y el SDK de Windows para x64 en el formato `--use-winsysroot-style --preserve-ms-arch-notation` de xwin. Los parámetros del script son las rutas de esas dependencias y el directorio de salida. El script produce `casterlabs-dock.dll`. El código se distribuye bajo GPL v2 o posterior, con el texto de la licencia en `fuente\COPYING`.

Este plugin comunitario se desarrolló con Codex. Puedes reportar problemas en la pestaña Issues del repositorio. Incluye tus versiones de OBS y Caffeinated y los pasos para reproducir el problema.
