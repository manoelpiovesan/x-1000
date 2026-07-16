# XPad Link

Sampler sincronizado via **Ableton Link** inspirado no X-Pad do Pioneer RMX-1000.

## Status

Todas as fases do `INSTRUCTIONS.md` estão implementadas:

| Fase | Descrição | Status |
|------|-----------|--------|
| 1 | Link: BPM/Beat em tempo real | ✅ |
| 2 | Engine de áudio (miniaudio) | ✅ |
| 3 | Entrada MIDI (RtMidi) | ✅ |
| 4 | Scheduler sincronizado ao Link | ✅ |
| 5 | Quantização (1/1 a 1/32) | ✅ |
| 6 | Time Stretch via Rubber Band | ✅ |
| 7 | GUI Dear ImGui + GLFW/OpenGL3 | ✅ |
| 8 | Configuração em JSON (nlohmann) | ✅ |

## Estrutura

```text
src/
  app/          App.hpp/cpp — orquestrador geral
  audio/        AudioEngine, AudioScheduler, TimeStretch
  config/       Config (JSON persistido)
  core/         Logger
  gui/          MainWindow (Dear ImGui)
  link/         LinkManager (Ableton Link / Simulation)
  midi/         MidiManager (RtMidi)
  samples/      Sample, SampleBank, SampleLoader
tests/
  LinkManagerTests.cpp  — testa Link, Scheduler, Quantização, Config
samples/
  pad1..pad8.wav        — banco padrão de demonstração
  snare/kick/clap/fx/   — estrutura organizada por categoria
third_party/
  imgui/        Dear ImGui
  miniaudio.h   miniaudio (header-only)
ableton-link/   SDK oficial do Ableton Link
```

## Dependências

```bash
sudo apt install librtmidi-dev librubberband-dev libglfw3-dev libsdl2-dev \
                 libasound2-dev nlohmann-json3-dev libgl-dev pkg-config
```

## Build

### Preparar SDK do Ableton Link (apenas na primeira vez)

```bash
git -C ableton-link submodule update --init --recursive
```

### Compilar com Ableton Link real (recomendado)

```bash
cmake --preset ableton-link
cmake --build --preset ableton-link
```

### Compilar sem SDK do Link (modo simulação)

```bash
cmake --preset default
cmake --build --preset default
```

## Executar

### Com GUI (padrão)

```bash
./build/xpad_link
```

### Headless (sem janela — útil para depuração ou servidores)

```bash
./build/xpad_link --headless --duration-seconds 10 --print-ms 250
```

### Modo simulação (sem Serato/Link real)

```bash
./build/xpad_link --simulation-only --headless --duration-seconds 5
```

### Opções disponíveis

| Argumento | Descrição |
|-----------|-----------|
| `--headless` | Executa sem GUI |
| `--duration-seconds N` | Duração em modo headless |
| `--print-ms N` | Intervalo de impressão headless |
| `--tempo BPM` | BPM inicial |
| `--simulation-only` | Ignora o Link real |
| `--config PATH` | Caminho para config JSON |

## Testes

```bash
ctest --preset ableton-link
```

Cobertura atual:
- `LinkManager`: avanço de beat, continuidade ao mudar BPM, projeção futura, modos
- `AudioScheduler`: disparo no beat correto, Hold/release, OneShot finish
- `Quantização`: todos os exemplos do `INSTRUCTIONS.md` (1/4 → 0.25 beats, etc.)
- `Config`: round-trip completo de todas as propriedades

## Configuração

O arquivo `xpad_config.json` é salvo automaticamente ao fechar.

Mapeamento MIDI padrão (Akai LPD8):

| Pad | Nota | Quantização |
|-----|------|-------------|
| 1 | 40 | 1/4 |
| 2 | 41 | 1/8 |
| 3 | 42 | 1/16 |
| 4–8 | 43–47 | 1/4 |

## Banco de samples

Coloque os arquivos em `samples/` com os nomes `pad1.wav` até `pad8.wav`,
ou use a estrutura por categoria:

```text
samples/
  snare/1_4.wav  1_8.wav  1_16.wav
  kick/1_4.wav   1_8.wav
  clap/1_4.wav   1_8.wav
  fx/1_4.wav     1_8.wav
```

Formatos aceitos: WAV, FLAC, MP3, OGG.

## Como usar com o Serato DJ Pro

1. Abra o Serato DJ Pro e ative **Ableton Link**
2. Execute `./build/xpad_link`
3. A janela exibirá: `Ableton Link: Connected` e o BPM atual
4. Pressione um pad na GUI ou no LPD8
5. O sample dispara quantizado conforme a grade do Link

> Se o DJ alterar o BPM no Serato, o sampler acompanha automaticamente
> via rate stretch, mantendo o pitch.

## Próximos passos planejados

- MIDI Learn (mapear qualquer controle em tempo real)
- Arrastar WAV para os pads
- Delay/Reverb sincronizados
- Roll e Gate automáticos
- Exportar/importar presets
