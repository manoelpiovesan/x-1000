# XPad Link

Bootstrap inicial da **Fase 1** do projeto descrito em `INSTRUCTIONS.md`.

## O que já existe

- Base de build com **CMake** e **C++20**
- Executável de console `xpad_link`
- Integração funcional com o **SDK real do Ableton Link** presente em `ableton-link/`
- `LinkManager` com dois modos:
  - **Simulation**: roda sem dependências externas para validar BPM/beat agora
  - **Ableton Link**: ponto de integração opcional para o SDK oficial
- Snapshot em tempo real com:
  - status de conexão
  - peer count
  - BPM
  - beat atual
- Testes simples cobrindo:
  - avanço do beat
  - continuidade ao mudar o BPM
  - projeção de beat futuro

## Estrutura atual

```text
src/
  app/
  core/
  link/
tests/
```

## Build local

### Linux / macOS

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

## Preparar o SDK local do Ableton Link

O repositório já inclui o código-fonte do SDK em `ableton-link/`, mas é preciso inicializar o submódulo `asio-standalone` após clonar:

```bash
git -C ableton-link submodule update --init --recursive
```

## Build com Ableton Link real

```bash
cmake --preset ableton-link
cmake --build --preset ableton-link
./build/xpad_link --duration-seconds 5 --print-ms 250
```

### Executar

```bash
./build/xpad_link --duration-seconds 5 --print-ms 250 --simulate-tempo-change --simulation-only
```

> Observação: se quiser apontar para outra cópia do SDK, sobrescreva `ABLETON_LINK_DIR` no configure.

## Próximos passos sugeridos

1. Validar a API exata do SDK do Ableton Link no ambiente alvo.
2. Trocar o modo de simulação pela captura real de sessão Link.
3. Introduzir `AudioScheduler` e testes de quantização antes da engine de áudio completa.
4. Adicionar MIDI (`RtMidi`) e engine de áudio (`miniaudio`).


