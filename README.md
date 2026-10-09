# Sueño-Guía

App personal para Android (sideload, sin Play Store) que lee tu **próxima alarma del Reloj de Samsung** y te avisa **9h30 antes** para que empieces a prepararte para dormir.

¿Por qué 9h30? Son **8 h de sueño** (objetivo) + **20 min** para dormirte + **~70 min** de rutina (cenar, lavarte los dientes, bajar pantallas). Es la hora en que *empezás a prepararte*, no la hora de apagar la luz.

> Mide la **oportunidad de sueño** (tiempo entre que te acostás y la alarma), no tu sueño real.

---

## Qué hace

- **F1 · Recordatorio principal** (activado): notificación 9h30 antes de tu alarma (ajustable de 8 h a 11 h, en pasos de 15 min). Muestra la hora de la alarma y a qué hora acostarte. Botones: **"Me voy a dormir"** y **"En 15 min"** (máximo 2 posposiciones).
- **F2 · Aviso previo suave** (desactivado por defecto): una frase tipo "Bajá las luces y dejá las pantallas", 30 min antes del F1 (ajustable de 15 a 60 min).
- **F3 · "Alarma cercana"** (activado): si ponés la alarma y ya quedó cerca del F1, te dice cuánto podés dormir si te acostás ya. Solo avisa si quedan **4 h o más**. Como máximo una vez cada 3 h.
- **F4 · Registro de noches** (activado): tocás **"Me voy a dormir"** y la app guarda la hora de acostarte. Al despertar muestra las últimas 7 noches y tu **deuda de sueño semanal**, contando solo las noches registradas. Objetivo por defecto: **8 h** (rango 7 h a 9 h). Guarda 90 noches.
- **F5 · Sugerencias para el finde** (activado, silenciosas): si tu alarma del sábado o domingo es más de 90 min posterior a la de la semana, te sugiere una hora tope para levantarte. Si el viernes o sábado a las 21:30 no hay alarma, te sugiere una hora de levantarte.

Ajustes generales: activar/desactivar todo, solo alarmas del Reloj (activado por defecto), borrar el registro.

### Lo que NO hace (a propósito)

- No usa internet. No tiene cuentas, nube, analytics ni exportación.
- No mide sueño real: no usa sensores, micrófono, acelerómetro ni Health Connect.
- No crea, edita ni lista tus alarmas. Solo lee la próxima del Reloj.
- No saltea "No molestar" ni los modos de Samsung, no pide permisos especiales de política y no te pide quitar la optimización de batería.
- No tiene notificación fija, servicio en primer plano, rachas, puntajes ni tono de culpa.

---

## Instalación en el Galaxy S25 FE

1. **Conseguí el APK.** Si compilaste vos, está en `build/sg.apk` (ver [Compilar](#compilar-desde-el-código)). Si lo bajás de GitHub, usá el archivo `sg.apk` de la release o del branch indicado.
2. **Copialo al teléfono** (cable USB, Google Drive, Bluetooth, lo que te sea más cómodo).
3. **Permití instalar desconocidas.** Al tocar el APK, Android te lo va a pedir. Activá **"Instalar apps desconocidas"** para la app desde la que lo abrís (Mis archivos o Chrome).
4. **Instalá.** Tocá el APK y después **Instalar**.
5. **Play Protect** puede avisar que el desarrollador es desconocido. Tocá **"Instalar de todas formas"**. Es normal en una app que no está en Play Store.

**Alternativa con ADB** (requiere depuración USB activada en *Opciones de desarrollador*):

```bash
adb install -r build/sg.apk
```

`-r` reemplaza la versión anterior y conserva los datos.

---

## Primer uso

1. **Abrí la app una vez.** Es obligatorio: Android no ejecuta los receptores de una app que nunca se abrió, así que si no la abrís, no vas a recibir nada.
2. **Permití notificaciones** cuando la app lo pida. Sin ese permiso no puede avisarte. Si lo rechazaste, aparece un banner con el botón para ir a los ajustes.
3. **Verificá "Próxima alarma"** en la pantalla principal. Tiene que mostrar la misma hora que la app Reloj. Si dice "No tenés alarma puesta", revisá que la alarma exista y esté activada.
4. **Poné la alarma en la app Reloj de Samsung como siempre.** La app la detecta sola.

---

## Ajustes de Samsung para que no te la duerma

Android y One UI pueden frenar la app en segundo plano y perderse el recordatorio. Hacé estos pasos una vez después de instalar, y de nuevo si los recordatorios dejan de llegar:

1. Ajustes → Batería → Límites de uso en segundo plano → **Aplicaciones que nunca se suspenden** → agregá **Sueño-Guía**.
2. Ajustes → Aplicaciones → Sueño-Guía → Batería → **Sin restricciones**.
3. En la pantalla de la app, desactivá **Pausar actividad de la app si no se usa**.
4. Opcional: en Ajustes → Batería → Límites de uso en segundo plano, desactivá **Poner en suspensión las aplicaciones sin uso**.

**Modo Dormir / No molestar:** la notificación igual llega y queda visible en el panel, pero sin sonido ni vibración. Si querés que suene durante el modo, agregá la app como excepción en la configuración de ese modo (Ajustes → Modos y rutinas → el modo → Notificaciones → apps permitidas).

---

## Cómo probar que anda

Hacé estas pruebas antes de confiar en la app. Tené en cuenta que la app **ignora alarmas fuera de la franja 04:00 a 12:00** (hora local) y alarmas de otras apps (si tenés "Solo alarmas del Reloj" activado).

1. **Lectura:** poné una alarma en el Reloj para dentro de 6 h (dentro de 04:00 a 12:00). Abrí Sueño-Guía. "Próxima alarma" debe mostrar esa hora.
2. **Aviso de alarma cercana (F3):** con esa misma alarma, esperá unos **90 s**. Debe llegar "Tu alarma quedó cerca" con las horas posibles de sueño. Si ya te llegó uno en las últimas 3 h, no va a aparecer otro (es el cooldown, es normal).
3. **Recordatorio principal (F1):** cambiá la alarma para dentro de **9 h 35 min**. En unos **5 min** debe llegar "Es hora de ir preparándote para dormir" con la hora para acostarte.
4. **Registro de noche (F4):** en esa notificación, tocá **"Me voy a dormir"**. Abrí la app: debe aparecer una noche nueva con tu hora de acostarte y la alarma.
5. **Posponer:** en la misma notificación probá **"En 15 min"**; debe volver a aparecer en 15 min (hasta 2 veces).
6. **Cancelar:** borrá la alarma en el Reloj y abrí la app. Debe decir "No tenés alarma puesta" y no quedar ningún recordatorio programado.

Después de borrar las pruebas, limpiá el registro desde Ajustes → "Borrar registro de noches".

---

## Compilar desde el código

**Requisitos:**
- Linux.
- JDK 17 o superior.
- Android SDK en `/opt/android-sdk`, con build-tools, la plataforma (android-37) y el NDK (r30, clang 21).
- Un archivo `/opt/android-sdk/toolchain.env` con las variables `BUILD_TOOLS`, `NDK_CLANG`, `NDK_SYSROOT`, `LLVM_BIN` y `ANDROID_JAR`. El Makefile no tiene rutas fijas: todo sale de ese archivo. Si está en otro lugar, usá `make apk TOOLCHAIN_ENV=/ruta/toolchain.env`.
- Para `make test` y `make valgrind`: `gcc` (con ASan/UBSan) y `valgrind`.
- Para `make lint`: `clang-tidy` (obligatorio) y `cppcheck` (opcional).

**Comandos:**

| Comando | Qué hace | Salida |
|---|---|---|
| `make apk` | Compila la librería nativa, los recursos, el Java, firma y valida el APK. Es el default. | `build/sg.apk` |
| `make test` | Tests del núcleo en C con ASan + UBSan, en tres zonas horarias (Buenos Aires, Madrid, UTC). | `build/host/sg_test_asan` |
| `make valgrind` | Mismos tests sin sanitizers, con valgrind (cero errores y cero fugas). | `build/host/sg_test_plain` |
| `make lint` | clang-tidy, cppcheck, búsqueda de funciones prohibidas y validación del APK (RELRO, BIND_NOW, 16 KB, BTI/PAC). | — |
| `make clean` | Borra `build/`. | — |

**Keystore de firma:** `make apk` genera `keystore/debug.keystore` la primera vez. **No se sube al repo** (está en `.gitignore`), así que guardalo bien. Si lo perdés o lo regenerás, la firma cambia y Android no te deja actualizar la app encima: tendrías que desinstalarla y perderías el registro de noches. Mientras uses el mismo keystore, podés instalar versiones nuevas con `adb install -r`.

---

## Privacidad y seguridad

- **Sin permiso de INTERNET.** La app no puede enviar nada a ningún lado.
- **Datos solo en el almacenamiento privado de la app.** Nada sale del teléfono.
- **Sin backup:** `allowBackup=false` y reglas de extracción que excluyen todo (nube y transferencia entre dispositivos).
- **Receptores no exportados:** todos `exported=false`. Solo la pantalla principal es exportada, porque es la que abre el lanzador.
- **Compilación nativa endurecida:** RELRO, BIND_NOW, NX, PAC + BTI, páginas de 16 KB, stack protector, `_FORTIFY_SOURCE=3`, inicialización de variables en cero.
- **Núcleo en C sin heap:** no hay `malloc` ni `strcpy` en el código nativo; todo son buffers fijos. Lo verifica `make lint`.
- **Tests bajo ASan, UBSan y valgrind.**

---

## Estructura del proyecto

```
Makefile                       build completo (make apk / test / valgrind / lint)
tools/lint-apk.sh              validación del APK
app/src/main/cpp/              núcleo en C (decisiones) + sg_jni.c (puente JNI)
app/src/main/java/ar/sg/       capa Java: ejecuta lo que decide el núcleo
app/src/main/res/              pantalla, textos en español, íconos
app/src/main/AndroidManifest.xml
tests/                         tests del núcleo en C
docs/                          especificaciones (ADVICE-features.md, ADVICE-architecture.md)
```

**Principio de diseño:** en C se decide todo (horas, textos, qué notificar); Java solo consulta al sistema, llama al núcleo y ejecuta los comandos que devuelve.

### Cómo se hizo

Lo construyó un enjambre de modelos de IA coordinado por Claude: un asesor de arquitectura, un asesor de funciones, operadores (Haiku) que escribieron cada pieza y asesores de auditoría que revisaron el resultado. Las especificaciones están en `docs/`.

---

## Problemas conocidos y limitaciones

- **Otras apps pueden tapar tu alarma del Reloj.** Si una alarma de otra app (calendario, temporizador) suena antes que la del Reloj, la app la toma como la próxima y no avisa por la del Reloj hasta que pase. Por eso "Solo alarmas del Reloj" está activado.
- **Las horas pueden atrasarse.** Si Android no permite alarmas exactas, el aviso puede llegar hasta 10 a 15 min tarde.
- **Las alarmas con posponer** pueden hacer que la app tome la hora de la posposición. Es un caso que la app contempla, pero conviene verlo en la prueba.
- **Hay que abrir la app después de un corte.** Si Android cierra la app a la fuerza, no corre nada hasta que la abras de nuevo.
- **No está probada en un teléfono físico** por quienes la construyeron. Al momento de este README, el repo todavía no tenía los tests (`tests/` está vacío), así que `make test` y `make apk` pueden fallar hasta completarlo. **Probá la primera noche y revisá que el aviso llegue.**
- **Mide oportunidad, no sueño.** El número de "sueño" es una estimación (tiempo en cama menos 20 min). No es un dato médico.
