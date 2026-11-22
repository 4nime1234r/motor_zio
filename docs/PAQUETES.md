# Paquetes de juego (.zgame)

Un paquete `.zgame` es un juego exportado desde el editor: sus escenas, modelos, texturas, audio y scripts compilados, cifrados en un solo archivo. Trae el nombre del juego, el autor y los demás datos en la cabecera, y puede ir firmado por su autor.

## Para el autor

1. **Proyecto > Información del juego...** Escribe el nombre, el autor o dueño, la versión, la descripción, el copyright, el sitio web, la licencia y la escena inicial.
2. **Proyecto > Exportar juego...** Elige la carpeta de salida, el nombre del archivo y la protección. Se crean dos archivos que se distribuyen juntos:
   - `MiJuego.exe`: el reproductor.
   - `MiJuego.zgame`: el juego cifrado.
3. Para jugar basta abrir `MiJuego.exe`. Si el juego tiene contraseña, la pide en una ventana.

Los scripts de C++ se compilan al exportar contra el `.exe` del juego, y los de C# se incluyen ya compilados. El código fuente de los scripts no va en el paquete, salvo que marques "Incluir el código fuente".

### Protección

| Modo | Qué hace |
| --- | --- |
| Contraseña | La clave sale de tu contraseña con Argon2id. Es el modo recomendado. Usa contraseñas largas: el cifrado es fuerte, pero una contraseña corta se puede adivinar probando. |
| Archivo de clave | Se crea un archivo `.zkey` con una clave aleatoria de 256 bits. Quien tenga el juego y el `.zkey` puede abrirlo (`MiJuego.exe --clave MiJuego.zkey`). No distribuyas el `.zkey` con el juego. |
| Sin contraseña | La clave se calcula a partir de datos del propio paquete. Solo ofusca: sirve para que nadie abra los archivos por accidente, no para protegerlos. |

### Firma de autor

Con "Firmar con mi clave de autor" el paquete lleva una firma Ed25519 sobre todo su contenido. El reproductor la comprueba y no abre un juego cuya firma no coincida, así que nadie puede cambiar el nombre, el autor ni el contenido sin romperla.

Tu clave de firma se crea la primera vez que exportas y se guarda en `%APPDATA%\ZioEditor\firma.key`. **Guárdala y no la compartas**: es lo que demuestra que los juegos son tuyos. Cada juego muestra la huella de tu clave (por ejemplo `9CC6-EA49-C855-BD85-A541-01CA-A79D-554C`). El nombre del autor es solo un texto, y cualquiera puede escribir el tuyo; lo que identifica a una persona es la huella. Publica tu huella en tu sitio web para que los jugadores puedan comprobarla.

### Permitir importar

Si marcas "Permitir abrir este juego en el editor", quien tenga el paquete y su contraseña o clave puede usar **Proyecto > Importar juego...** para crear un proyecto editable. Es una decisión del editor, no una barrera criptográfica: quien conoce la clave puede descifrar el contenido con cualquier herramienta que lea el formato.

## Qué protege y qué no

- **Protege** el juego mientras está guardado o se copia: sin la contraseña o la clave no se puede leer, y cualquier cambio se detecta.
- **No puede proteger** frente a quien ejecuta el juego con la contraseña o la clave: el reproductor tiene que descifrarlo para ejecutarlo. Mientras el juego corre, sus archivos están descifrados en una carpeta temporal privada (`ZioGame_...`), que se borra al salir. Un juego que se puede jugar siempre se puede extraer con paciencia suficiente, en cualquier motor.
- En Windows, la DLL de los scripts de C++ puede quedar en esa carpeta hasta la siguiente vez que se abra un juego, porque el sistema no deja borrar una DLL en uso.
- Los scripts de C++ compilados desde el editor son de Windows, así que un juego con scripts de C++ se juega solo en Windows. Un juego sin ellos se puede abrir también en Linux con `zio_player juego.zgame`.

## Formato

Todos los números son little-endian.

```
magia            8 bytes   "ZIOPKG" 0x00 0x01
versión          u32       1
protección       u8        0 archivo de clave, 1 contraseña, 2 sin contraseña
banderas         u8        bit 0: firmado
reservado        u16       0
argon memoria    u32       KiB (solo con contraseña)
argon pasadas    u32       (solo con contraseña)
sal              32 bytes
prefijo nonce    16 bytes
meta longitud    u32
meta             JSON UTF-8 con nombre, autor, versión, descripción, copyright,
                 sitio web, licencia, id, versión del motor, escena inicial,
                 ejecutable, fecha y permiso de importar
clave pública    32 bytes  (solo si está firmado)
tamaño de bloque u32       1 MiB
tamaño en claro  u64
bloques          [u32 longitud][16 bytes MAC][datos cifrados] ...
firma            64 bytes  (solo si está firmado)
```

- **Cifrado:** XChaCha20-Poly1305 por bloques. El nonce de cada bloque es el prefijo más el número de bloque, y los datos autenticados son el hash BLAKE2b de toda la cabecera, el número de bloque y si es el último. Así no se pueden cambiar la cabecera, reordenar bloques ni truncar el archivo sin que falle el descifrado.
- **Clave:** con contraseña, Argon2id (256 MiB, 3 pasadas) y la sal de la cabecera; después se deriva la clave de cifrado con BLAKE2b.
- **Firma:** Ed25519 sobre el BLAKE2b-512 de todos los bytes anteriores a la firma.
- **Contenido:** una lista de archivos `[u16 longitud de ruta][ruta][u64 tamaño][datos]` que termina con una ruta de longitud 0. Al extraer se rechazan rutas absolutas, con `..`, con `:` o `\`, nombres reservados de Windows y rutas repetidas.

La implementación está en `src/pack/Package.cpp` y usa [Monocypher](https://monocypher.org) (`third_party/monocypher`).
