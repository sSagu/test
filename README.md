# Sueño-Guía

App personal para Android (sideload, sin Play Store) que lee tu **próxima alarma del Reloj de Samsung** y te avisa **9h30 antes** para que empieces a prepararte para dormir.

¿Por qué 9h30? Son **8 h de sueño** (objetivo) + **20 min** para dormirte + **~70 min** de rutina (cenar, lavarte los dientes, bajar pantallas). Es la hora en que *empezás a prepararte*, no la hora de apagar la luz.

> Mide la **oportunidad de sueño** (tiempo entre que te acostás y la alarma), no tu sueño real.

---

## Qué hace

**Pantalla principal:** la hora de tu próxima alarma en grande, a qué hora te avisa la app, el botón **"Me voy a dormir"**, el gráfico de las últimas 7 noches y tu deuda de sueño de la semana. El engranaje de arriba a la derecha abre **Ajustes**, en su propia pantalla.

- **F1 · Recordatorio principal** (activado): notificación 9h30 antes de tu alarma (ajustable de 8 h a 11 h, en pasos de 15 min). Muestra la hora de la alarma y a qué hora acostarte. Botones: **"Me voy a dormir"** y **"En 15 min"** (máximo 2 posposiciones). Si reiniciás el teléfono mientras el aviso sigue vigente, Android lo borra y la app lo vuelve a mostrar **en silencio**, con el tiempo que le quedaba.
- **F2 · Aviso previo suave** (desactivado por defecto): una frase tipo "Bajá las luces y dejá las pantallas", 30 min antes del F1 (ajustable de 15 a 60 min).
- **F3 · "Alarma cercana"** (activado): si ponés la alarma y ya quedó cerca del F1, te dice cuánto podés dormir si te acostás ya. Solo avisa si quedan **4 h o más**. Como máximo una vez cada 3 h.
- **F4 · Registro de noches** (activado): tocás **"Me voy a dormir"** (en el aviso o en la pantalla principal) y la app guarda la hora de acostarte. La pantalla principal muestra las últimas 7 noches en un gráfico y tu **deuda de sueño semanal**, contando solo las noches registradas. Objetivo por defecto: **8 h** (rango 7 h a 9 h). Guarda 90 noches.
- **F5 · Sugerencias para el finde** (activado, silenciosas): si tu alarma del sábado o domingo es más de 90 min posterior a la de la semana, te sugiere una hora tope para levantarte. Si el viernes o sábado a las 21:30 no hay alarma, te sugiere una hora de levantarte.

**Ajustes** (engranaje, pantalla propia): activar/desactivar todo, anticipación del aviso y horas de sueño objetivo (con − y +), aviso previo, alarma cercana, sugerencias para el finde, solo alarmas del Reloj (activado por defecto) y borrar el registro.

### Lo que NO hace (a propósito)

- No usa internet. No tiene cuentas, nube, analytics ni exportación.
- No mide sueño real: no usa sensores, micrófono, acelerómetro ni Health Connect.
- No crea, edita ni lista tus alarmas. Solo lee la próxima del Reloj.
- No saltea "No molestar" ni los modos de Samsung, no pide permisos especiales de política y no te pide quitar la optimización de batería.
- No tiene notificación fija, servicio en primer plano, rachas, puntajes ni tono de culpa.

---

## Instalación en el Galaxy S25 FE

1. **Conseguí el APK.** Ya viene compilado y firmado en [`dist/sueno-guia.apk`](dist/sueno-guia.apk): abrí ese archivo en GitHub desde el celu y tocá **Download** (o "View raw"). Si compilás vos, queda en `build/sg.apk` (ver [Compilar](#compilar-desde-el-código)).
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
3. **Verificá la pantalla principal.** La hora grande de "Próxima alarma" tiene que ser la misma que la de la app Reloj. Si dice "No tenés alarma puesta", revisá que la alarma exista y esté activada.
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

**Antes de empezar (una sola vez):**
1. Abrí Sueño-Guía apenas la instalás y tocá **Permitir** (y aceptá el diálogo de Android). Hasta que la abras una vez, Android no le avisa nada. Si ves "Las notificaciones están desactivadas. No vas a recibir avisos.", tocá **Abrir ajustes** y activalas.
2. Ajustes de Android → Aplicaciones → Sueño-Guía → Batería → **Sin restricciones**. Fijate también que no esté en las listas de aplicaciones en suspensión.

**Reglas que tenés que saber:**
- La app mira solo la **próxima** alarma del Reloj. La usa si suena entre **04:00 y 12:00** (ambas incluidas) y faltan **36 h o menos**.
- Una alarma fuera de esa franja aparece igual con su hora, pero la pantalla dice "Tu próxima alarma es a las HH:MM" y "Solo te aviso para alarmas entre las 04:00 y las 12:00". Revisá que sea a. m. (04:00 y no 16:00).
- El aviso sale a la hora de la alarma menos "Avisarme antes de la alarma" (9 h 30 min de fábrica, de 8 h a 11 h). Se cambia en **Ajustes** (engranaje arriba a la derecha) con − y +. La pantalla muestra solo la hora, sin el día.
- Si dice "Hay una alarma de otra app a las HH:MM. Solo tengo en cuenta las alarmas del Reloj.", desactivá "Solo alarmas del Reloj" en **Ajustes**.

**Comprobación instantánea (a cualquier hora):** poné tu alarma real y abrí la app. Ejemplo: alarma a las 08:00 → "Próxima alarma" con 08:00 y "Te aviso para prepararte a las 22:30".

**Prueba real del aviso.** Elegí la fila según la hora actual. Ponés la alarma de prueba en el Reloj y después volvés a la app:

| Hora actual | Anticipación | Alarma de prueba | El aviso llega |
|---|---|---|---|
| 04:00 a 16:55 | 11 h (6 toques en +) | 04:00 del día siguiente | a las 17:00 |
| 16:55 a 18:25 | 11 h (6 toques en +) | ahora + 11 h 05 min | en unos 5 min |
| 18:25 a 02:25 | 9 h 30 (no toques nada) | ahora + 9 h 35 min | en unos 5 min |
| 02:25 a 03:55 | 8 h (6 toques en −) | ahora + 8 h 05 min | en unos 5 min |

(Entre las 03:55 y las 04:00 esperá unos minutos y usá la primera fila.)

- "Te aviso para prepararte a las" tiene que mostrar la hora de la última columna. Si tardás hasta 30 min en poner la alarma, el aviso sale enseguida.
- Llega "Es hora de ir preparándote para dormir" con "Alarma a las HH:MM. Para dormir 8 h 00 min, acostate a las (alarma − 8 h 20 min)." Con anticipación de 8 h el texto es "Alarma a las HH:MM. Si te acostás ahora, podés dormir 7 h 30 min.".
- Tocá los botones del aviso. Si tocás el cuerpo, se abre la app y el aviso desaparece, pero en la pantalla principal sigue el botón **"Me voy a dormir"** (ver la prueba de abajo).
  1. **En 15 min:** el aviso desaparece y vuelve 15 min después del toque. Funciona 2 veces; a la segunda vuelta ya no está el botón.
  2. **Me voy a dormir:** cierra el aviso y anota la noche bajo el día de la alarma. Si tocás antes de las 00:00, la noche aparece en el gráfico recién después de medianoche.
- **Registro sin notificación:** deslizá el aviso para descartarlo, abrí la app y tocá **"Me voy a dormir"**. El botón está disponible desde 2 h antes del aviso hasta 1 h antes de la alarma (antes dice "Disponible desde las HH:MM"). Tiene que aparecer "Anotado: te acostaste a las HH:MM" y la noche en el gráfico al día siguiente de la alarma, como arriba.
- (Opcional) Aviso de alarma cercana: con la anticipación de fábrica, poné una alarma que suene dentro de 4 h 22 min a 9 h (siempre entre 04:00 y 12:00). Unos 90 s después llega "Tu alarma quedó cerca". No aparece si hubo otro aviso en las últimas 3 h.

**Limpieza (siempre):**
1. Volvé la anticipación a "9 h 30 min antes (rango 8 h a 11 h)".
2. Borrá la alarma de prueba en el Reloj. Si queda, suena de verdad y tapa tu alarma real.
3. Engranaje → Ajustes → "Borrar registro de noches" → **Borrar**.
4. Abrí la app: con tu alarma real, "Te aviso para prepararte a las" tiene que mostrar (alarma − 9 h 30). Si la alarma está a más de 36 h (por ejemplo, el lunes visto desde el viernes), dice "Cuando falte menos de un día y medio, acá vas a ver a qué hora te aviso." hasta que falten 36 h. Es normal.

**Esto no es una falla:**
- Después de que salió el aviso, la app dice "Aviso enviado a las HH:MM".
- Con No molestar o con el modo Dormir de Samsung activo, el aviso llega sin sonido. Agregá Sueño-Guía como excepción del modo o mirá el panel de notificaciones.

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
- **Si reiniciás el teléfono**, Android borra las notificaciones. La app vuelve a mostrar el aviso en silencio solo si seguís dentro de la ventana del botón y no pasaron 3 h desde que llegó. Si no, el aviso no vuelve: podés anotar la noche desde la pantalla principal con "Me voy a dormir", mientras esté disponible.
- **No está probada en un teléfono físico** (no había uno disponible al construirla). El núcleo en C sí está probado en la PC: tests unitarios con ASan/UBSan en 3 zonas horarias, valgrind sin leaks, clang-tidy y cppcheck limpios, y dos auditorías (Android y núcleo). **La primera noche, revisá que el aviso llegue.**
- **Mide oportunidad, no sueño.** El número de "sueño" es una estimación (tiempo en cama menos 20 min). No es un dato médico.
