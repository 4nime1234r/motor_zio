<p align="center">
  <img src="assets/icon/zio.png" alt="Zio" width="128">
</p>

# Zio

Zio es un motor de juegos 2D y 3D escrito en C++20 para Windows y Linux. Renderiza con Vulkan e incluye un editor visual (por ahora solo para Windows) para montar escenas sin escribir código.

Es un proyecto en desarrollo activo. La versión actual es la **0.4.1**.

Zio es de uso libre, pero no se puede vender: puedes usarlo y modificarlo gratis, dando crédito a su autor, y no puedes venderlo ni cobrar por él. Mira la sección [Licencia](#licencia).

## Características

**Gráficos 2D**
- Sprites, texturas y textos con un sistema de nodos jerárquico.
- Animación de sprites con hojas de cuadros y animaciones con nombre (bucle y velocidad), guardadas en la escena.
- Escenas guardadas y cargadas desde archivos JSON.

**Gráficos 3D**
- Escenas con jerarquía de nodos y cámara.
- Carga de modelos `.obj`, `.gltf` y `.glb`: posiciones, normales, coordenadas de textura, transformaciones de nodos y colores de vértice. Los modelos con varios materiales se dibujan con el material de cada parte.
- Materiales PBR (metálico, rugosidad, mapas de normales, emisivo y oclusión), texturas externas o incrustadas, modos de transparencia (opaco, máscara y mezcla) y doble cara.
- Cámaras y luces del archivo glTF y morph targets, con pesos fijos o animados desde el archivo.
- Antialiasing MSAA (4 muestras por defecto), descarte de objetos fuera de la cámara y dibujo instanciado en GPU de mallas estáticas repetidas, también en las sombras.
- Prefabs: escenas reutilizables dentro de otras.
- Animación de huesos: varios esqueletos por modelo, hasta 8 huesos por vértice, interpolación lineal, por pasos y de curva cúbica, y transición suave entre animaciones. La animación en curso se guarda en la escena.
- Iluminación por píxel con luz direccional, luces puntuales y luces focales (hasta 8 por escena, con color, intensidad y alcance) y sombras reales con mapa de sombras suavizado en las tres.
- Materiales transparentes ordenados correctamente.
- Líneas con grosor para depuración.

**Sistemas del motor**
- Física 2D con cuerpos estáticos y dinámicos, rectángulos y círculos, gravedad, impulsos y rebote.
- Capas, máscaras de colisión, triggers con eventos de entrada y salida y articulaciones (distancia, bola, bisagra y fija) en la física 3D, y distancia, soldadura, capas y triggers en la 2D.
- Física 3D con cuerpos rígidos (estáticos, dinámicos y cinemáticos), esferas, cápsulas, cajas orientadas y mallas de triángulos estáticas, rotación con inercia, fricción, rebote, impulsos y fuerzas, y rayos.
- Scripts de C++ y de C# para los nodos, con `onStart`, `onUpdate` y `onDestroy` (`OnStart`, `OnUpdate` y `OnDestroy` en C#), que se guardan en la escena. Los de C++ y los de C# se compilan solos desde el editor y se ejecutan también con el botón Jugar (ver [Scripts](#scripts)).
- Audio con clips, volumen y tono, sonido posicional en 3D con oyente, mezcla por buses con sub-buses y efectos por bus (reverberación, paso bajo y paso alto en Windows).
- Ventana con pantalla completa, maximizada o normal (`Window::setMode`) y selector de archivos del sistema (`platform::openFileDialog`).
- Entrada de teclado, ratón y mando.
- Librería de interfaz propia (`zio::ui`): widgets en árbol con contenedores de diseño (cajas, paneles, desplazamiento, divisores y pestañas), botones, casillas, listas desplegables, campos de texto con selección y portapapeles, árbol con selección múltiple, barra de menús con submenús y menús contextuales, tooltips, campos numéricos arrastrables, selector de color, texto de varias líneas, rejilla de iconos, iconos con tinte, diálogos, widget de visor para incrustar el 3D, fuente en negrita y área acoplable con paneles a izquierda, derecha o abajo. Tiene un tema con la paleta de Zio y usa las fuentes del sistema. Funciona en juegos y es la base del editor.
- Sistema de tareas en segundo plano con aviso al hilo principal.
- Pila de escenas, bus de eventos, preferencias en disco y matemáticas (vectores, matrices y cuaterniones).

## Scripts

Zio tiene dos lenguajes de scripts, C++ y C#, y un lugar preparado para añadir el tuyo.

**Crear un script.** En el panel Archivos, clic derecho y Nuevo script de C++ o Nuevo script de C#. Escribes el nombre, se crea con su plantilla, se añade al nodo seleccionado y se abre en tu editor de código.

**Compilar.** No hay que hacer nada: el editor compila cuando guardas un script y antes de Jugar, y recarga los scripts sin reiniciar. Los errores salen en el panel Salida. También puedes compilar a mano en Proyecto > Compilar scripts.

**Cualquier editor.** El proyecto incluye la configuración para VS Code, clangd, Visual Studio y Rider (`.clangd`, `.vscode` y `Scripts.csproj`). Proyecto > Abrir proyecto en el editor de código lo abre, y Proyecto > Elegir editor de código te deja usar el programa que prefieras.

**Herramientas.** C++ usa el compilador de Visual Studio Build Tools y C# usa el SDK de .NET 8 o superior. Si falta alguno, el editor ofrece instalarlo con un clic. Las cabeceras del motor van dentro del propio editor, así que no hay carpetas extra ni CMake.

**Tu propio lenguaje.** Un lenguaje es una clase que hereda de `zio::scripting::ScriptLanguage` (`include/zio/scripting/Language.h`): dice qué extensiones usa, su plantilla, cómo se compila y cómo se carga. El archivo `src/scripting/ZioLanguage.cpp` trae el esqueleto listo para rellenar. Se activa con `-DZIO_ENABLE_ZIO_LANGUAGE=ON` y, una vez activo, aparece solo en el menú Nuevo script, en la compilación automática y en el Inspector.

## Exportar, importar y jugar

**Proyecto > Información del juego** guarda el nombre, el autor o dueño, la versión, el copyright, el sitio web y la licencia en el proyecto. **Proyecto > Exportar juego** crea el ejecutable del juego y su paquete `.zgame`, cifrado con XChaCha20-Poly1305 y Argon2id y, si quieres, firmado con tu clave de autor. **Proyecto > Importar juego** abre un paquete como proyecto editable cuando el autor lo permitió, y **Proyecto > Jugar un juego exportado** lo lanza.

El reproductor pide la contraseña, comprueba la firma y muestra el nombre y el autor del juego. En Linux se usa `zio_player juego.zgame`. El formato y lo que protege y no protege el cifrado están explicados en [docs/PAQUETES.md](docs/PAQUETES.md).

## Editor

El editor vive en `tools/zio_editor` y está hecho con `zio::ui`.

**Proyectos y escenas**
- Crear y abrir proyectos. Cada proyecto es una carpeta con `project.zio` y subcarpetas de modelos, texturas, audio, scripts y escenas.
- Abrir, guardar y guardar como escenas 3D, con aviso si hay cambios sin guardar.
- Botón Jugar para ver la escena con su cámara y detenerla restaurando el estado editado.
- Deshacer y rehacer hasta 200 pasos.

**Paneles**
- **Escena:** árbol de nodos con filtro y botón para añadir nodos vacíos, cubos y modelos `.obj`, `.gltf` y `.glb`.
- **Sistema de archivos:** lista plana con todos los archivos del proyecto, sin carpetas, con filtro. Crear, editar y eliminar aparecen únicamente al hacer clic derecho. Cada archivo nuevo va a la carpeta que le corresponde según su tipo, y los scripts de C++ nuevos traen su plantilla.
- **Visor 3D:** cuadrícula, selección por clic y gizmos para mover, rotar y escalar, con la pestaña de la escena arriba.
- **Inspector:** secciones plegables con posición, rotación y escala; color, escala UV y sombra de las mallas; animación de huesos (elegir, reproducir, bucle y velocidad) y scripts de cada nodo. Las animaciones se ven moverse también fuera del modo Jugar.
- **Salida:** registro con hora de lo que hace el editor.
- **Rendimiento:** milisegundos por fotograma, memoria, llamadas de dibujo y tareas.
- Los paneles se pueden colocar a la izquierda, a la derecha o abajo desde el menú Ver.

**Edición**
- Selección múltiple, copiar, pegar y duplicar.

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

El editor se actualiza solo:

- Al abrirlo, y después cada 4 horas, comprueba si hay una versión nueva en las releases del repositorio. También puedes comprobarlo cuando quieras desde **Ayuda > Buscar actualizaciones**.
- Si hay una, aparece el aviso **Actualización disponible** arriba a la derecha y un mensaje en la pestaña Salida. Al pulsar el aviso se abre un cuadro con las novedades de esa versión.
- Pulsa **Descargar** y verás el progreso. La descarga se verifica con su huella SHA-512 antes de aceptarse; si no coincide, se descarta.
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

Para compilar y ejecutar las pruebas automáticas:

```
cmake -S . -B build -DZIO_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
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

Un script de C++ para un nodo:

```cpp
#include "zio/scene3d/Node3D.h"
#include "zio/scripting/Script.h"

class Girar : public zio::scripting::Script
{
public:
    void onUpdate(float dt) override
    {
        node().setRotation(zio::math::Quaternion::fromAxisAngle({0.f, 1.f, 0.f}, 90.f * dt) * node().rotation());
    }
};

ZIO_SCRIPT(Girar)
```

Los scripts se adjuntan a los nodos por su nombre, desde el Inspector del editor o con `node->addScript("Girar")`, y se ejecutan cuando el juego actualiza la escena.

Un script de C# (archivo `.cs` en `assets/scripts`):

```csharp
using System.Numerics;
using Zio;

public class Girar : Script
{
    public override void OnUpdate(float dt)
    {
        Node.Rotation = Quaternion.CreateFromAxisAngle(Vector3.UnitY, dt) * Node.Rotation;
    }
}
```

En el editor, **Proyecto > Compilar scripts** los compila con `dotnet`, muestra los errores en la pestaña Salida y deja los scripts disponibles en el Inspector. Desde un juego propio: `zio::scripting::DotNetRuntime::instance().start(carpeta)`, `buildScripts(proyecto, ...)` y `loadScripts(...)`. En C# se pueden leer y cambiar la posición, rotación y escala del nodo, crear y destruir nodos (`Node.Create`, `Destroy`, `FindChild`, `Instantiate`), usar la física 3D (`Body`, `Physics`), reproducir sonidos (`Audio.Play`), cargar escenas (`Scenes.Load`), leer teclado y ratón, y escribir en el registro con `Log.Info`.

La carpeta `projects/` contiene ejemplos completos, entre ellos `Prueba3D`, una escena 3D con sombras y transparencias, y `PruebaUI`, una pantalla con la librería de interfaz propia.

## Limitaciones actuales

- El editor solo funciona en Windows. En Linux los juegos abren ventana y dibujan con Vulkan (X11 y Wayland). El texto usa las fuentes TrueType que haya instaladas, y los scripts compilados desde el editor siguen siendo solo de Windows.
- No hay cielo ni iluminación por imagen: los metales reflejan solo un degradado ambiental y las luces de la escena.
- Los cambios hechos a los nodos de dentro de un prefab instanciado no se guardan en la escena.
- La animación de huesos se calcula en la CPU.

## Estructura

```
include/zio/      Cabeceras públicas del motor
src/              Código del motor y shaders
tools/zio_editor  El editor
tests/            Pruebas automáticas
projects/         Proyectos de ejemplo
cmake/            Scripts y plantillas de CMake
third_party/      Componentes de terceros con sus licencias
```

## Contribuir

Si quieres ayudar, sigue los pasos de [CONTRIBUTING.md](CONTRIBUTING.md): crear un fork, una rama, compilar y probar, y enviar un pull request.

## Publicar una versión

Esto lo hace el autor: sube el número de `VERSION` en `CMakeLists.txt`, escribe las novedades en `RELEASE_NOTES.md` y haz push. La release se crea sola, y el editor muestra esas notas en el aviso de actualización.

## Licencia

Zio es de uso libre con atribución y sin venta. Copyright (c) 2026 4nime1234r.

Puedes usarlo, modificarlo de cualquier manera y distribuirlo de forma gratuita, siempre que des crédito a 4nime1234r como autor original de Zio. **No puedes vender Zio**, ni versiones modificadas, ni cobrar por su uso, descarga o soporte, ni incluirlo en un producto que se venda como motor o herramienta de desarrollo. Los juegos que hagas con Zio son tuyos y puedes distribuirlos y cobrar por ellos.

- **En tus juegos y aplicaciones:** un crédito visible, por ejemplo en la pantalla de créditos, con un texto como "Hecho con Zio, de 4nime1234r".
- **En versiones modificadas del motor o del editor:** indica de forma clara, en su README y en su "acerca de", que están basadas en Zio de 4nime1234r, y enlaza a este repositorio.
- **En cualquier copia:** conserva el aviso de copyright y el archivo [LICENSE](LICENSE).

No puedes presentar Zio, ni una versión modificada, como obra tuya. Consulta el archivo [LICENSE](LICENSE) para los términos completos.

Para cifrar y firmar los paquetes se usa [Monocypher](https://monocypher.org) (BSD de 2 cláusulas o CC0), incluido en `third_party/monocypher`.

En Linux el motor usa [GLFW](https://www.glfw.org) (licencia zlib) para la ventana y la entrada, [stb](https://github.com/nothings/stb) (dominio público) para imágenes y fuentes, y [miniaudio](https://miniaud.io) (dominio público) para el audio. Se descargan al configurar con CMake.

Los iconos del editor son de [Lucide](https://lucide.dev), gratuitos y de licencia abierta (ISC, y MIT para los derivados de Feather). El texto de su licencia está en `third_party/icons/LICENSE-lucide.txt`.
