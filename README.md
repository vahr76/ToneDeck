# ToneDeck — Neural Amp & IR Multi-FX

Plugin de guitarra que carga **capturas NAM** (incluida la nueva arquitectura **A2 / slimmable**), **modelos AIDA-X** (.json de RTNeural) y **respuestas de impulso (IR)** de gabinete, con una interfaz estilo pedalera multiefectos compacta: pantalla a color con la cadena de bloques, perillas asignables y footswitches.

![ToneDeck](docs/screenshot.png)

Formatos: **VST3** (Windows / macOS / Linux), **AU** (macOS), **CLAP**, **LV2** (Linux) y **aplicación Standalone**.

## Cadena de señal

```
IN → NR (gate) → DRV (modelo de pedal) → AMP (modelo de ampli) → EQ → CAB (IR) → DLY → REV → MASTER
```

| Bloque | Qué carga / hace | Perillas |
|---|---|---|
| **NR** | Noise gate con histéresis, detecta sobre la señal limpia | Threshold, Release |
| **DRV** | Segundo slot de modelo (captura de pedal: TS, Klon, fuzz…) | Drive, Level, Param 1/2*, Quality* |
| **AMP** | Modelo principal `.nam` o `.json` | Gain, Volume, Param 1/2*, Quality* |
| **EQ** | Tone stack post-ampli | Bass, Middle, Treble, Presence, Mid Freq |
| **CAB** | IR `.wav/.aiff/.flac` (normalizada, re-muestreada automáticamente) | Low Cut, High Cut, Mix, Level |
| **DLY** | Delay estéreo con repeticiones oscurecidas | Time, Feedback, Mix |
| **REV** | Reverb estéreo | Size, Damping, Width, Mix |

\* Las perillas marcadas aparecen sólo si el modelo las admite: **Param 1/2** para modelos AIDA-X condicionados (entrada de 2 o 3 canales) y **Quality** para modelos NAM A2 "slimmable" (baja la calidad para ahorrar CPU).

## TONE3000

El botón **TONE3000** conecta el plugin con tu cuenta de [tone3000.com](https://www.tone3000.com):

- **Elegir tono en el navegador**: se abre TONE3000, elegís un tono y ToneDeck descarga sus modelos a `Library/TONE3000/<tono>/` y lo carga en el bloque seleccionado (AMP o DRV; las IR van solas al CAB). Si el tono trae varias capturas, te deja elegir cuál.
- **Mis favoritos / descargados / subidos, Populares, Nuevos**: listas de tu cuenta para cargar tonos sin salir del plugin.
- La primera vez te pide tu **clave pública** (`t3k_pub_...`, se crea en *Settings → API Keys*). Nunca pegues la clave secreta (`t3k_cs_...`).
- El login usa OAuth con PKCE y una dirección local (`127.0.0.1`), así que tu contraseña nunca pasa por el plugin. La sesión queda guardada en el archivo de ajustes de ToneDeck.

## Uso

- **Clic en un bloque** de la pantalla → lo selecciona y las 5 perillas pasan a controlarlo.
- **Doble clic o clic derecho en un bloque** → encender / apagar. Los 4 footswitches hacen lo mismo con DRV, AMP, DLY y REV.
- **Cargar archivos**: botón `LOAD`, clic en el recuadro del nombre de archivo, o **arrastrar y soltar** sobre la pantalla (`.nam/.json` van al AMP, o al DRV si lo soltás sobre ese bloque; `.wav` van al CAB).
- **`<` `>`**: pasa al archivo anterior / siguiente de la misma carpeta — ideal para comparar capturas descargadas de TONE3000.
- **`LIBRARY`**: lista en pantalla todos los modelos o IRs de tu carpeta de biblioteca (incluye subcarpetas). Por defecto `Documentos/ToneDeck/Library`; se cambia con *CHANGE FOLDER* y se recuerda entre sesiones.
- **`NORMALIZE`**: iguala el volumen de los modelos NAM que traen metadato de loudness (a -18 dB), como hace el plugin oficial de NAM.
- **Presets**: `SAVE` guarda todo (parámetros + rutas de modelos/IR) en `Documentos/ToneDeck/Presets`. `< PRESET` / `PRESET >` los recorre. Los mensajes **MIDI Program Change** cargan el preset N (en orden alfabético).
- La ventana se puede **redimensionar** (50 %–200 %) desde la esquina inferior derecha.

### Detalles técnicos

- Los modelos se cargan y pre-calientan en un hilo aparte y se intercambian sin bloquear el audio (con un fade-in corto, sin clicks).
- Si el DAW corre a una frecuencia distinta a la del modelo (casi siempre 48 kHz), se re-muestrea internamente con filtros anti-alias; la latencia (16 muestras) se informa al host.
- Entrada mono (canal izquierdo), salida estéreo. Funciona en pistas mono o estéreo.
- Archivos `.json` que en realidad son exportaciones NAM también se detectan automáticamente.

## Compilar

Requisitos: CMake ≥ 3.22, Git y un compilador C++20 (Visual Studio 2022, Xcode 14+, o GCC 11+/Clang 14+).

```bash
bash scripts/fetch_deps.sh          # descarga JUCE, NAM Core, Eigen, RTNeural, CLAP (versiones fijadas)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target ToneDeck_All
```

Los binarios quedan en `build/ToneDeck_artefacts/Release/` (VST3, AU, CLAP, LV2, Standalone).

En Linux instalá antes: `libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl-dev`.

**Sin compilar nada:** subí esta carpeta a un repositorio de GitHub; el workflow de `.github/workflows/build.yml` compila Windows, macOS (universal Intel + Apple Silicon) y Linux automáticamente y deja los instaladores en la pestaña *Actions*. Si creás un tag `v1.0.0` publica un Release con los zips.

### Tests

```bash
cmake -S . -B build -DTONEDECK_BUILD_TESTS=ON
cmake --build build --target ToneDeckTests
./build/ToneDeckTests_artefacts/Release/ToneDeckTests salida/ modelo1.nam modelo2.json ...
```

Procesa audio sintético por toda la cadena a 44.1/48/96 kHz con distintos tamaños de buffer, verifica guardado/restauración del estado y genera capturas de la interfaz.

## Instalación

| Sistema | VST3 | Otros |
|---|---|---|
| Windows | `C:\Program Files\Common Files\VST3\` | CLAP: `C:\Program Files\Common Files\CLAP\` |
| macOS | `/Library/Audio/Plug-Ins/VST3/` | AU: `/Library/Audio/Plug-Ins/Components/` · CLAP: `/Library/Audio/Plug-Ins/CLAP/` |
| Linux | `~/.vst3/` | LV2: `~/.lv2/` · CLAP: `~/.clap/` |

En macOS, si el plugin no firmado aparece bloqueado: `xattr -dr com.apple.quarantine ToneDeck.vst3`.

## Licencias

- [JUCE](https://juce.com) — AGPLv3 o licencia comercial. Para uso personal no hay problema; si vas a distribuir el plugin cerrado necesitás licencia de JUCE.
- [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) — MIT
- [RTNeural](https://github.com/jatinchowdhury18/RTNeural) — BSD-3
- [Eigen](https://eigen.tuxfamily.org) — MPL2
- [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) — MIT

ToneDeck no está afiliado a TONE3000, Neural Amp Modeler ni AIDA DSP; sólo lee sus formatos de archivo abiertos.
