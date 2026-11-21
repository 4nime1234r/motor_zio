<p align="center">
  <img src="assets/icon/zio.png" alt="Zio" width="128">
</p>

# Zio

Zio es un motor de juegos 2D y 3D escrito en C++20 para Windows. Renderiza con Vulkan e incluye un editor visual para montar escenas sin escribir código.

Es un proyecto en desarrollo activo. La versión actual es la **0.2.0**.

## Características

**Gráficos 2D**
- Sprites, texturas y textos con un sistema de nodos jerárquico.
- Escenas guardadas y cargadas desde archivos JSON.

**Gráficos 3D**
- Carga de modelos `.obj`, `.gltf` y `.glb` (posiciones, normales, coordenadas de textura, transformaciones de nodos, color base y textura base externa del material).
- Escenas con jerarquía de nodos y cámara.
- Iluminación por píxel y sombras reales con mapa de sombras suavizado.
- Materiales transparentes ordenados correctamente.
- Líneas con grosor para depuración.

**Sistemas del motor**
- Física 2D con cuerpos estáticos y dinámicos, rectángulos y círculos, gravedad, impulsos y rebote.
- Física 3D con cuerpos rígidos (estáticos, dinámicos y cinemáticos), esferas y cajas orientadas, rotación con inercia, fricción, rebote, impulsos y fuerzas, y rayos.
- Audio con clips, volumen y tono, por sonido o por categoría.
- Entrada de teclado, ratón y mando.
- Librería de interfaz propia (`zio::ui`): widgets en árbol con contenedores de diseño (cajas, paneles, desplazamiento, divisores y pestañas), botones, casillas, listas desplegables, campos de texto con selección y portapapeles, árbol con selección múltiple, barra de menús con submenús y menús contextuales, tooltips, campos numéricos arrastrables, selector de color, texto de varias líneas, rejilla de iconos, widget de visor para incrustar el 3D, fuente en negrita y área acoplable con paneles a izquierda, derecha o abajo, tema con la paleta de Zio y fuentes del sistema. Funciona en juegos y es la base del futuro editor.
- Sistema de tareas en segundo plano con aviso al hilo principal.
- Pila de escenas, bus de eventos, preferencias en disco y matemáticas (vectores, matrices y cuaterniones).

## Editor

El editor vive en `tools/zio_editor` y está hecho con `zio::ui`, la librería de interfaz propia del motor.

- Crear y abrir proyectos. Cada proyecto es una carpeta con `project.zio` y subcarpetas de modelos, texturas y escenas.
- Abrir, guardar y guardar como escenas 3D, con aviso si hay cambios sin guardar.
- Añadir nodos vacíos, cubos y modelos `.obj`, `.gltf` y `.glb`.
- Panel de archivos del proyecto, con árbol de carpetas y cuadrícula de iconos por tipo (escenas, modelos, texturas, audio y scripts de C++, C# y Lua).
- Visor 3D con cuadrícula, selección por clic y gizmos para mover, rotar y escalar.
- Selección múltiple, copiar, pegar y duplicar.
- Botón Jugar para ver la escena con su cámara y detenerla restaurando el estado editado.
- Inspector de posición, rotación, escala y color.
- Deshacer y rehacer hasta 200 pasos.

### Controles del visor

| Acción | Cómo |
|---|---|
| Girar la cámara | Botón derecho y arrastrar |
| Desplazar la cámara | Botón central y arrastrar |
| Acercar y alejar | Rueda del ratón |
| Volar por la escena | W, A, S, D; Q y E para bajar y subir; Mayús para ir más rápido |
| Seleccionar | Clic izquierdo; con Ctrl se añade o quita de la selección |
| Mover, rotar o escalar | Teclas 1, 2 y 3 y arrastrar el gizmo; con Ctrl, a pasos fijos |
| Enfocar la selección | F |
| Deseleccionar | Esc |
| Eliminar | Supr |
| Copiar, pegar y duplicar | Ctrl+C, Ctrl+V y Ctrl+D |
| Guardar | Ctrl+S |
| Deshacer y rehacer | Ctrl+Z y Ctrl+Y |
| Jugar y detener | F5 |
| Menú contextual | Clic derecho rápido, sin arrastrar |

## Descargar

Cada versión se publica en la sección **Releases** del repositorio, con el editor ya compilado para Windows y el código fuente de esa versión.

## Actualizaciones

El editor se actualiza solo, igual que Zion:

- Al abrirlo, y después cada 4 horas, comprueba si hay una versión nueva en las releases del repositorio.
- Si la hay, aparece **Actualización disponible** en la barra superior. Pulsa **Descargar** y verás el progreso.
- La descarga se verifica con su huella SHA-512 antes de aceptarse; si no coincide, se descarta.
- Cuando termina, pulsa **Reiniciar y actualizar**. Si prefieres no hacerlo ahora, la actualización se instala sola al cerrar el editor.

La carpeta donde esté `zio_editor.exe` debe permitir escribir. Si falla, el editor te lo indica y puedes reintentar o descargar la release a mano.

## Compilar desde el código

Necesitas Windows, Visual Studio 2022 (o un compilador de C++20), CMake 3.20 o superior y el SDK de Vulkan:

```
winget install KhronosGroup.VulkanSDK
```

Después:

```
cmake -S . -B build
cmake --build build --config Release
```

El editor queda en `build\tools\zio_editor\zio_editor.exe`. Puedes abrirlo con un proyecto concreto:

```
zio_editor.exe C:\ruta\a\MiProyecto
```

## Usar el motor en tu juego

El motor se enlaza como la biblioteca `zio::engine`. Un proyecto mínimo:

```cmake
cmake_minimum_required(VERSION 3.20)
project(MiJuego LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

add_subdirectory(ruta/a/Zio engine)

add_executable(MiJuego main.cpp)
target_link_libraries(MiJuego PRIVATE zio::engine)
```

La carpeta `projects/` contiene ejemplos completos, entre ellos `Prueba3D`, una escena 3D con sombras y transparencias, y `PruebaUI`, una pantalla con la librería de interfaz propia.

## Limitaciones actuales

- No hay animación de huesos ni de sprites.
- De los modelos glTF solo se usa el primer material (color base y textura externa); no hay PBR completo, huesos ni texturas incrustadas.
- No hay scripting: el juego se escribe en C++.
- Solo funciona en Windows.

## Estructura

```
include/zio/      Cabeceras públicas del motor
src/              Código del motor y shaders
tools/zio_editor  El editor
projects/         Proyectos de ejemplo
cmake/            Scripts y plantillas de CMake
```

## Licencia

Zio es software propietario. Copyright (c) 2026 4nime1234r. Todos los derechos reservados.

Puedes compilarlo y usarlo para crear tus propios juegos y distribuirlos sin regalías, pero no puedes copiar, modificar ni redistribuir el motor o su código. Consulta el archivo [LICENSE](LICENSE) para los términos completos.
