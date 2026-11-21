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
- Animación de sprites con hojas de cuadros, animaciones con nombre (bucle, velocidad) guardadas en la escena.
- Animación de huesos: varios esqueletos por modelo, hasta 8 huesos por vértice, interpolación lineal, por pasos y de curva cúbica, y transición suave entre animaciones. La animación en curso se guarda en la escena y el editor la reproduce desde el Inspector, también fuera del modo Jugar.
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
- Interfaz propia: panel Escena con filtro de nodos y Sistema de archivos a la izquierda, visor 3D con la pestaña de la escena en el centro, Inspector con secciones plegables a la derecha, y abajo Salida (mensajes del editor) y Rendimiento. Los paneles se pueden colocar a la izquierda, a la derecha o abajo.
- Sistema de archivos: una lista plana con todos los archivos, sin carpetas, con filtro, donde solo se puede crear, editar o eliminar, y esas opciones aparecen únicamente al hacer clic derecho. Cada archivo nuevo va a la carpeta que le corresponde según su tipo.
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

- La animación de huesos se calcula en la CPU.
- De los modelos glTF solo se usa el primer material (color base y textura externa); no hay PBR completo, huesos ni texturas incrustadas.
- Los scripts son de C++ y se compilan con el juego; todavía no hay scripts de C#. El editor guarda qué scripts tiene cada nodo, pero solo los ejecuta el juego.
- Solo funciona en Windows.

## Estructura

```
include/zio/      Cabeceras públicas del motor
src/              Código del motor y shaders
tools/zio_editor  El editor
projects/         Proyectos de ejemplo
cmake/            Scripts y plantillas de CMake
```

Para publicar una versión nueva: sube el número de `VERSION` en `CMakeLists.txt`, escribe las novedades en `RELEASE_NOTES.md` y haz push. El editor muestra esas notas en el aviso de actualización (menú Ayuda > Buscar actualizaciones).

## Licencia

Zio es de uso libre con atribución. Copyright (c) 2026 4nime1234r.

Puedes usarlo, modificarlo de cualquier manera, distribuirlo y venderlo, incluso en proyectos comerciales, siempre que des crédito a 4nime1234r como autor original de Zio: en tus juegos, con un crédito visible como "Hecho con Zio, de 4nime1234r", y en cualquier versión modificada del motor, indicando que está basada en Zio y enlazando a este repositorio. Consulta el archivo [LICENSE](LICENSE) para los términos completos.

Los iconos del editor son de [Lucide](https://lucide.dev), gratuitos y de licencia abierta (ISC, y MIT para los derivados de Feather). El texto de la licencia está en `third_party/icons/LICENSE-lucide.txt`.
