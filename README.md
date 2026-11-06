# Black Magic — Dungeon Estudio

Corrector de tono tipo Auto-Tune clásico (VST3) para FL Studio. Con **Retune Speed = 0** el efecto es
el más robótico posible: la voz salta de inmediato a la nota exacta de la escala.

## Controles
| Control | Qué hace |
|---|---|
| **Retune Speed** (0–100) | 0 = salto instantáneo (robot). Más alto = más natural. |
| **Throat** (0–100, 50 = neutro) + botón **OFF/ON** | Mueve los formantes sin cambiar la nota (±3 semitonos). Apagado por defecto: con OFF no toca nada. |
| **Tracking** (0–100) | Cómo sigue el detector a la voz. Bajo = más estable y relajado; alto = más rápido y estricto. |
| **Humanize** (0–100) | Las notas largas se corrigen más despacio (deja pasar vibrato natural). En 0 no hace nada. |
| **Input** (LCD) | Low Male, Alto/Tenor, Female: rango de voz que busca el detector. |
| **Key** (LCD) | C, C#, D … B |
| **Scale** (LCD) | Chromatic, Major, Minor, Harmonic Minor, Pentatonic Major, Pentatonic Minor |

La pantalla LCD tiene un anillo de **tuning** que se llena desde el centro hacia donde desafinas (hielo cuando estás afinado, rojo cuando estás lejos), con la **nota de salida** al centro (promediada en ~100 ms y con fade al cortar la voz). A los lados: **cents** de desviación y **Hz** de tu voz. Todo con glow. Haz click en INPUT TYPE / KEY / SCALE para abrir las opciones. La ventana se puede redimensionar.

Para el sonido T-Pain / robot: Retune 0, Scale Chromatic (o la tonalidad de tu canción), Tracking 50, Humanize 0.

## Compilar en GitHub (Windows)
1. Sube **todo** el contenido de esta carpeta a un repositorio (incluida la carpeta `.github`).
2. Pestaña **Actions** → corre automáticamente en cada push (o "Run workflow").
3. Al terminar, descarga el artefacto **BlackMagic-VST3-Windows**: dentro está `Black Magic.vst3`.
4. Copia esa carpeta `Black Magic.vst3` a `C:\Program Files\Common Files\VST3\` y en FL Studio haz *Options → Manage plugins → Find installed plugins*.

Fuentes: copia tus archivos `.otf`/`.ttf` (UnifrakturCook y Helvetica) a la carpeta `Resources/` y se incrustan solos (ver `Resources/LEEME.txt`). El logo Dungeon ya va incluido.

## Notas técnicas
- Latencia: se informa a FL Studio (compensación automática, no se muestra en el plugin). ~15 ms Female, ~27 ms Alto/Tenor, ~37 ms Low Male a 48 kHz.
- Detección YIN + cambio de tono TD-PSOLA (preserva formantes), lectura con interpolación sinc de 32 puntos: no recorta agudos. Sin pitch, el audio pasa idéntico bit a bit (solo retrasado).
- JUCE se descarga solo (versión fijada en `CMakeLists.txt`, línea `GIT_TAG`). JUCE es AGPL/comercial: si vas a vender el plugin revisa su licencia.
