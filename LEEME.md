# BLACK MAGIC  -  Dungeon Estudio
Procesador de correccion de pitch (autotune) VST3 para FL Studio.

## Pantalla LCD
- **INPUT TYPE / KEY / SCALE**: botones desplegables dentro de la LCD (clic -> se abre la lista).
  - Input Type: Low Male (75-350 Hz) / Alto-Tenor (100-600 Hz) / Female (160-1000 Hz) / Broadcast (75-1000 Hz).
  - Scale: Major, Minor, Chromatic, Harmonic Minor, Pentatonic Major, Pentatonic Minor, Blues.
- **TUNING**: medidor de afinacion centrado (como en Antares). Muestra cuanto se aleja tu voz de la nota a la que se corrige:
  barra hacia la DERECHA = cantas sostenido (alto), hacia la IZQUIERDA = bemol (bajo). Rango +-50 cents; verde = casi afinado.
- **NOTE**: nota de salida, promediada: es la nota mas votada en los ultimos ~0.33 s y solo cambia cuando la nueva
  nota se mantiene ~0.13 s; se apaga cuando termina la voz.

## Perillas y botones
- **RETUNE SPEED** (0-100): 0 = salto instantaneo a la nota (efecto robotico clasico). 100 = correccion lenta y natural.
- **HUMANIZE** (0-100 %): en notas largas la correccion se hace mas lenta para conservar el vibrato natural.
- **TRACKING** (0-100 %): bajo = el detector reacciona rapido y es estricto; alto = tolera voces con ruido o inestables.
- **THROAT** + boton **ON/OFF**: cambia el tamano de la "garganta" (formantes) sin cambiar la nota.
  Con el boton en **OFF** (por defecto) el efecto esta completamente apagado: maxima transparencia, y la perilla se ve apagada.
  Al activarlo, el audio completo pasa por el procesador de formantes (50 % = neutro; menos = voz mas grave/grande; mas = mas aguda/pequena, +-3 semitonos).
- **LOW LATENCY**: para GRABAR con el plugin puesto. Reduce el retardo (usa grains mas cortos), a cambio de algo de calidad.
  Apagado = mejor calidad. Debajo del boton se muestra la latencia real y se informa a FL Studio para que la compense.

| Modo | Latencia (a 44.1 kHz) |
|---|---|
| Normal (todas las voces) | 31.5 ms |
| Low Latency, Female | 10.5 ms |
| Low Latency, Alto/Tenor | 16.3 ms |
| Low Latency, Low Male / Broadcast | 21.5 ms |
En Low Latency la latencia depende de la nota mas grave que cubre el tipo de voz elegido.

Doble clic en una perilla la devuelve a su valor por defecto. La interfaz no muestra ventanitas al pasar el raton.
Para el efecto mas robotico: Retune 0, Humanize 0 y escala Chromatic (o la tonalidad de tu cancion).

## Afinacion y transparencia
- Las decisiones de afinacion (periodo, nota, "hay voz") se aplican **alineadas en el tiempo con el audio al que describen**
  (antes se aplicaban unos 10-20 ms desfasadas): ya no queda sin corregir el final de cada nota ni se cuela el vibrato.
- Con Retune 0 el periodo de salida es exactamente el de la nota objetivo.
- El primer trozo de cada frase/nota se centra en el pulso de la voz (los agudos se conservan); NO se vuelve a "buscar" el pulso
  continuamente (probado: eso hacia oscilar la afinacion con voces reales).
- Por encima de 5.5 kHz el audio original pasa sin tocar (conserva el aire); la rama corregida se limpia por encima de 8 kHz.
- Con Throat OFF no se interpola ni se modifica nada fuera de la correccion de nota.
- Pruebas automaticas (`tests/`): `engine_test.cpp` (afinacion exacta, escalas, tipos de voz, Throat, Low Latency),
  `hf_test.cpp` (respuesta de agudos, 3 frecuencias de muestreo) y `singer_test.cpp` (cantante sintetico con vibrato, glissandos,
  formantes cambiantes y voz aspera: precision en notas sostenidas, periodicidad, clics y agudos).

## Estetica
- Titulo en tipografia gotica **UnifrakturCook Bold** (licencia SIL OFL, incluida en `Assets/OFL_UnifrakturCook.txt`; se incrusta en el plugin).
- Resplandor (bloom) en las perillas, medidor, nota, LEDs de los botones y titulo.
- Texto de la interfaz: **Helvetica Neue Display Bold** si esta instalada; si no, Helvetica/Arial.
  Fuente con licencia, no se puede incluir: copia tu archivo a `Assets/ui_font.otf` (o `.ttf`) y vuelve a compilar y se incrusta sola.
- Logo: reemplaza `Assets/dungeon_logo.png` por el tuyo (PNG transparente, ~480x225 px o mas).

## Compilar SIN instalar nada (GitHub Actions)
1. En tu repositorio de GitHub sube TODO el contenido de esta carpeta reemplazando lo anterior
   (incluida la carpeta oculta `.github` y la carpeta `Assets`).
2. Pestana **Actions** -> "Compilar Black Magic (Windows VST3)" (unos 10 min).
3. Descarga **BlackMagic-VST3-Windows** desde "Artifacts".

## Instalar en FL Studio
Copia la carpeta **Black Magic.vst3** a `C:\Program Files\Common Files\VST3` y en FL Studio:
Options -> Manage plugins -> Find installed plugins. Si cambias de modo (Low Latency) o de tipo de voz con el proyecto
abierto, el plugin avisa del cambio de latencia; si notas desfase, reinicia la reproduccion.

## Notas
- Funciona con UNA voz/instrumento monofonico a la vez (no acordes).
- El parametro MIX existe para automatizacion pero no tiene perilla en el diseno (100 % por defecto).
- Pruebas del motor sin JUCE: `g++ -std=c++17 -O2 tests/engine_test.cpp -o t && ./t` (igual con `hf_test.cpp` y `singer_test.cpp`).
- Licencia: JUCE es AGPLv3 o comercial. Uso personal sin problema; si vas a vender o distribuir el plugin revisa https://juce.com/legal/juce-8-licence/
