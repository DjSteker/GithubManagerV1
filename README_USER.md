

Guía rápida — qué hace la aplicación
- Gestor Git (GTK4) permite clonar, inicializar, commitear y hacer push de repositorios Git y opcionalmente guardar el token de acceso en un fichero XML cifrado por repositorio. La clave maestra que introduces se usa para cifrar y descifrar el token.

Requisitos previos
- Linux con:
  - g++ (C++17), make o CMake
  - libgtk-4-dev
  - libtinyxml2-dev
  - libssl-dev (OpenSSL)
  - git instalado en PATH
- Compilar (ejemplo mínimo):
  sudo apt install build-essential pkg-config libgtk-4-dev libtinyxml2-dev libssl-dev
  g++ -std=c++17 -O2 src/*.cpp -o GithubManager $(pkg-config --cflags --libs gtk4 tinyxml2 openssl) -pthread

Arrancar la aplicación
- ./GithubManager
- Se abre la ventana con campos: Clave Maestra, Directorio Local, URL Remoto, Rama, Mensaje Commit, Token GitHub y opción “Guardar credenciales cifradas”.

1) Clave maestra — qué es y cómo obtenerla
- Qué hace: la clave maestra es la contraseña que usará la app para cifrar (encriptar) y descifrar el token guardado en un archivo XML por repositorio.
- Requisitos de la clave:
  - Usa una frase larga y única (por ejemplo 16+ caracteres con mezcla: mayúsculas, minúsculas, números y símbolos) o una passphrase tipo 3-4 palabras aleatorias.
  - No la compartas ni la guardes en texto plano en el equipo.
- Generarla:
  - Manualmente: crea una frase que puedas recordar y que no reutilices.
  - Generador seguro (ejemplo): openssl rand -base64 24
    - Esto genera 32 bytes codificados en base64 — guárdalo en un gestor de contraseñas si no lo memorizas.
- Consecuencias de perderla:
  - Si pierdes la clave maestra, no podrás descifrar tokens guardados en XML (no hay “backdoor”). La app no almacena la clave maestra.

2) Rutas locales y cómo especificarlas
- campo “Directorio Local”: ruta absoluta o relativa donde quieres clonar o gestionar el repo.
  - Ejemplo: /home/usuario/proyectos/mirepo
- Comportamiento:
  - Si el directorio no existe y proporcionas una URL remota, la app intentará clonar allí.
  - Si el directorio existe y contiene un repositorio (.git), la app realizará commit/push en ese repositorio.
  - Si el directorio existe y NO es vacío y le pides clonar, la app detectará que no está vacío y evitará clonar directamente; en su lugar inicializará el repo o te avisará.
- Dónde se guardan las configuraciones cifradas:
  - Por defecto en XDG_CONFIG_HOME, si está definido:
    $XDG_CONFIG_HOME/gestorgit/repos
  - Si no existe XDG_CONFIG_HOME:
    ~/.config/gestorgit/repos
  - Cada repositorio se guarda como un archivo XML cuyo nombre es un hash (SHA256 parcial) de la ruta local. El archivo que corresponde a un directorio se obtiene internamente con GestorConfig::archivoPara(directorio). Para listar todos los XML: ~/.config/gestorgit/repos/*.xml

3) URL remota (GitHub) — formatos válidos
- HTTPS:
  - https://github.com/OWNER/REPO.git
  - Recomendación: no incluyas credenciales en la URL (no uses https://user:token@github.com/...)
- SSH:
  - git@github.com:OWNER/REPO.git
  - Es más seguro usar SSH (clave SSH) para evitar almacenar tokens.
- Restricciones:
  - La app valida y rechaza URLs HTTPS que contengan credenciales embebidas (con '@'). Si la URL contiene credenciales, no se permitirá por seguridad.

4) Rama (branch)
- Campo “Rama”: rama en la que quieres pushear. Si lo dejas vacío, la app asume:
  - Para operaciones de push: rama por defecto "main".
  - Para pull cuando no se especifica: usa HEAD.
- Comportamiento push:
  - Si subes por primera vez, se ejecuta: git push -u origin <rama>
  - El flag -u configura upstream para futuros pushes.

5) Mensaje de commit — por qué y qué poner
- ¿Por qué incluir un mensaje?
  - El mensaje describe el cambio y queda en el historial. Es buena práctica siempre poner un mensaje claro.
- Si dejas el campo vacio:
  - La app usa un mensaje por defecto: "Actualización".
- Buenas prácticas para el mensaje:
  - Breve línea principal (<= 72 caracteres) que describa la intención: "Arregla fallo de sincronización" o "Agrega README".
  - Opcionalmente, añade cuerpo más detallado si hay muchos cambios (no soportado por UI multilineal, por tanto usa una frase informativa).

6) Token de GitHub — cómo obtenerlo y qué scopes necesita
- ¿Qué es? Un token personal (Personal Access Token - PAT) permite autenticación HTTPS para operaciones push/pull en GitHub.
- Pasos para obtenerlo en la web:
  1. Accede a github.com y entra en Settings → Developer settings → Personal access tokens → Tokens (classic) → Generate new token (note: GitHub promueve tokens finos, lee la documentación).
  2. Elige el tipo de token: si solo trabajas con repositorios públicos puedes usar scope public_repo; para repos privados necesitas repo (full control of private repositories).
  3. Selecciona scopes mínimos:
     - Para pushes y pulls: repo (o public_repo para repos públicos).
     - Si necesitas admin:repo_hook o workflow solo si se requiere.
  4. Genera el token y cópialo una sola vez (GitHub no lo mostrará después).
- Alternativa con gh CLI (recomendado si lo usas):
  - gh auth login --with-token  (o gh auth refresh)
  - gh auth status
  - gh auth token
- Cómo introducirlo en la app:
  - Pega el token en el campo “Token GitHub”.
  - Si marcas “Guardar credenciales cifradas”, la app encriptará el token con la clave maestra y lo guardará en el XML correspondiente.
- Seguridad:
  - Usa el token con scopes mínimos necesarios.
  - Rota tus tokens periódicamente.
  - Prefiere SSH cuando sea posible para evitar almacenar tokens.

7) Qué sucede cuando marcas “Guardar credenciales cifradas”
- La app encripta el token (Cifrado::encriptar) con la Clave Maestra y guarda el texto cifrado en el archivo XML (ubicado en ~/.config/gestorgit/repos/<hash>.xml).
- Al seleccionar el directorio (selector de carpeta), la app intentará cargar esa configuración y, si hay token cifrado y has escrito la clave maestra en la UI, intentará descifrarlo y rellenar el campo token.
- Importante: la clave maestra no se guarda. Si la olvidas no podrás recuperar tokens.

8) Flujo típico de uso (paso a paso)
- Caso A: clonar y subir un repo existente
  1. Abrir app.
  2. En Clave Maestra introduce tu passphrase.
  3. En Directorio Local especifica la carpeta destino (ej: /home/usuario/proyectos/mi-repo).
  4. En URL Remoto pega https://github.com/miuser/mirepo.git o git@github.com:miuser/mirepo.git.
  5. En Rama deja "main" o pon otra rama.
  6. Introduce Mensaje Commit si quieres.
  7. En Token GitHub pega tu PAT (solo si usas HTTPS). Si usas SSH no es necesario.
  8. Marca “Guardar credenciales cifradas” si deseas almacenar el token cifrado.
  9. Pulsa “⬆ SUBIR / SINCRONIZAR”.
  10. Observa el log y la etiqueta estado. Al finalizar la operación, revisa que el /tmp/.ghmgr_askpass_* (si fue usado) haya sido eliminado — la app intenta limpiar esos archivos.

- Caso B: inicializar un repo local y subirlo por primera vez
  1. Si el directorio existe y no contiene .git, la app puede inicializarlo y añadir remote origin si proporcionaste URL remota.
  2. Se ejecutará git init, git add -A, git commit -m "<mensaje>" y git push -u origin <rama>.

9) Dónde están los ficheros de configuración (ruta exacta)
- Ruta base (según implementación):
  - Si XDG_CONFIG_HOME definido: $XDG_CONFIG_HOME/gestorgit/repos
  - Si no: ~/.config/gestorgit/repos
- Nombre del archivo:
  - archivoPara(dir) calcula SHA256 del string del directorio y usa los primeros 16 bytes hex como nombre: <hex>.xml
  - Ejemplo: ~/.config/gestorgit/repos/3a7b4c... .xml
- Para listar:
  - ls ~/.config/gestorgit/repos/*.xml

10) Seguridad y recomendaciones
- Nunca incluyas token en la URL remota.
- Usa claves SSH si no quieres manejar tokens.
- Genera tokens con los scopes mínimos.
- Guarda la clave maestra en un gestor de contraseñas si no la memorizarás.
- Rotar tokens periódicamente y revocar tokens comprometidos.
- Evita ejecutar la app en entornos compartidos si guardas tokens.

11) Recuperación y resolución de problemas comunes
- Error “URL inválida: no se permiten credenciales embebidas”:
  - Solución: usa https://github.com/OWNER/REPO.git sin user:token@ o usa SSH.
- Error “El directorio destino existe y no está vacío; no se puede clonar allí.”:
  - Solución: vacía el directorio o cambia el Directorio Local a uno vacío, o inicializa repo local y añade remote.
- Error “No se pudo ejecutar el comando git”:
  - Verifica que git está instalado y en PATH.
  - Revisa permisos en el directorio destino.
- Si la aplicación se queda atascada con popen/git:
  - Revisa el log en la UI; si el proceso hijo está colgado, reinicia la app.
  - Comprueba /tmp para ver si hay scripts askpass residuales y elimínalos manualmente: rm /tmp/.ghmgr_askpass_*
- Si no puedes descifrar token guardado:
  - Comprueba que la clave maestra que introduces sea exactamente la misma que usaste para cifrar (sensible a mayúsculas/minúsculas).
  - Si no la recuerdas, no hay forma de recuperarla; regenera un PAT en GitHub y actualiza la configuración.

12) Notas avanzadas para administradores / desarrolladores
- La app actualmente ejecuta git mediante shell (popen). A futuro se recomienda migrar a libgit2 para evitar inyección de comandos y mejorar control.
- Los ficheros askpass temporales se crean en /tmp y la app intenta sobreescribirlos con ceros y borrarlos; en sistemas con COW/SSD esto no garantiza eliminación física.
- Para debugging:
  - Ejecutar la app desde terminal y observar stdout para mensajes adicionales.
  - Revisar los XML en ~/.config/gestorgit/repos para inspeccionar metadatos (token cifrado no legible).

13) Ejemplos prácticos
- Ejemplo URL pública (HTTPS):
  https://github.com/miuser/mirepo.git
- Ejemplo URL SSH:
  git@github.com:miuser/mirepo.git
- Ejemplo Directorio local:
  /home/miuser/dev/mirepo

14) Resumen de buenas prácticas
- Usa SSH cuando puedas. Si usas token:
  - Genera un PAT con scope mínimo (public_repo o repo).
  - Guárdalo cifrado con la clave maestra.
  - No compartas ni copies el token en sitios inseguros.
- Mantén el software actualizado (cuando se crea conveniente) y revisa logs si algo falla.

