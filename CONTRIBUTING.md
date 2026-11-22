# Cómo contribuir a Zio

Gracias por querer ayudar. Para que todas las contribuciones se puedan revisar y juntar sin problemas, sigue estos pasos.

## 1. Antes de empezar

- Busca en los [issues](https://github.com/4nime1234r/motor_zio/issues) si ya existe lo que quieres hacer.
- Si es un cambio grande (una función nueva, un cambio de diseño), abre primero un issue para comentarlo con el autor. Así no pierdes tiempo en algo que no se vaya a aceptar.
- Los arreglos pequeños (un error, un texto, una prueba que falta) se pueden enviar directamente.

## 2. Preparar tu copia

1. Haz un **fork** del repositorio en GitHub.
2. Clona tu fork:
   ```
   git clone https://github.com/TU_USUARIO/motor_zio.git
   cd motor_zio
   ```
3. Crea una rama para tu cambio, con un nombre que lo describa:
   ```
   git checkout -b arreglo-sombras-3d
   ```
   No trabajes directamente sobre `main`.

## 3. Compilar y probar

Necesitas Windows, Visual Studio 2022 con las herramientas de C++, CMake 3.20 o superior y el SDK de Vulkan.

```
cmake -S . -B build -DZIO_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

El ejecutable del editor queda en `build/tools/zio_editor`.

## 4. Reglas del código

- C++20. Sigue el estilo del código que ya hay: mismos nombres, mismas llaves y misma forma de organizar los archivos.
- **No escribas comentarios en el código.** Usa nombres claros en su lugar.
- Los textos que ve el usuario (editor, mensajes) van en español.
- Un cambio, un propósito. No mezcles un arreglo con una limpieza o con una función nueva.
- Toda función nueva o error corregido debe llevar su prueba en la carpeta `tests` y agregarse a `tests/CMakeLists.txt`.
- No subas archivos generados, compilados ni de tu editor (`build`, `.vs`, `.idea`, etc.).
- No añadas dependencias nuevas sin hablarlo antes. Si el cambio incluye código o recursos de terceros, deben tener una licencia que permita usarlos aquí, y su texto debe ir en `third_party`.
- No cambies el número de `VERSION` en `CMakeLists.txt` ni el archivo `RELEASE_NOTES.md`: de las versiones y sus notas se encarga el autor.

## 5. Antes de enviar

Comprueba esta lista:

- [ ] Compila sin errores.
- [ ] Todas las pruebas pasan (`ctest`).
- [ ] Probaste el cambio en el editor o en un juego, si afecta a lo que se ve o se usa.
- [ ] No hay comentarios nuevos en el código.
- [ ] El cambio hace una sola cosa.

## 6. Enviar el cambio

1. Haz commits pequeños, con un mensaje corto y claro que diga qué cambia, por ejemplo: `Corrige la sombra de los modelos con escala negativa`.
2. Sube tu rama a tu fork:
   ```
   git push origin arreglo-sombras-3d
   ```
3. En GitHub, abre un **pull request** hacia la rama `main` de este repositorio y rellena la plantilla: qué cambia, por qué y cómo lo probaste.
4. Espera la revisión. La integración continua compila el proyecto y ejecuta las pruebas; si falla, corrígelo en la misma rama y sube los cambios, y el pull request se actualiza solo.
5. Atiende los comentarios de la revisión. Cuando esté todo bien, el autor lo integra.

## 7. Licencia y crédito

Zio es de uso libre con atribución y sin venta (consulta [LICENSE](LICENSE)). Al enviar tu contribución aceptas que se publique bajo esa misma licencia. Tu nombre quedará en el historial del repositorio.

Si tu pull request incluye contenido de terceros, indícalo en la descripción.

## 8. Cómo comportarse

Sé respetuoso con quien revisa y con el resto de contribuyentes. Las críticas son al código, no a las personas.
