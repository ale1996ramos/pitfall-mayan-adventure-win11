# Pitfall: The Mayan Adventure — correção para Windows 10/11

[English](README.md)

Faz a versão de Windows 95 (1995) de *Pitfall: The Mayan Adventure* (Activision / Kinesoft) rodar no Windows 10 e 11 com **efeitos sonoros, música do CD e configurações salvas**, sem precisar de administrador, modo de compatibilidade, DxWnd ou CD no drive.

| Problema no Windows moderno | Corrigido |
|---|---|
| Nenhum som, opções de áudio desabilitadas | ✅ |
| Efeitos cortam uns aos outros / só um som por vez | ✅ |
| A `WAIL32.DLL` original (Miles) trava ou picota | ✅ substituída por uma implementação em DirectSound |
| Música exige o CD original no drive | ✅ CD virtual que toca arquivos MP3/WAV |
| Precisa "Executar como administrador" para salvar configurações | ✅ configurações gravadas no usuário atual |

Como foi feito, patch por patch: [docs/TECHNICAL.md](docs/TECHNICAL.md) (em inglês).

## Requisitos

* A versão de Windows 95 do jogo, instalada ou copiada para uma pasta (`PITFALL.EXE` com 710.144 bytes, `WAIL32.DLL`, `INIT.PH`, `LEVEL00.PH` … `LEVEL24.PH`).
* Para a música: as faixas de áudio do CD do jogo em `.mp3` (ou `.wav`).

## Instalação

1. Baixe o `PitfallFix-<versão>.zip` em [Releases](../../releases) e extraia.
2. Copie o `PitfallFix.exe` para a pasta do jogo (onde está o `PITFALL.EXE`) e execute.
   Ele confere a versão do jogo, faz backup dos originais como `*.orig`, aplica as correções no `PITFALL.EXE` e grava `WAIL32.DLL`, `PITMM.DLL` e `PITMUSIC.INI`.
3. Música: crie uma pasta `music` ao lado do `PITFALL.EXE` com as faixas de áudio do CD nomeadas `Track02.mp3` … `Track24.mp3` (a faixa 1 é a de dados).
   Vai usar a trilha lançada com 19 faixas? Use o `PITMUSIC-soundtrack-example.ini` como seu `PITMUSIC.INI`.
4. Abra o `PITFALL.EXE` normalmente (sem modo de compatibilidade). Nas **Propriedades** do jogo, ative *Sound Effects* e *CD Music*.

Para desfazer tudo: `PitfallFix.exe --uninstall`.

## Configuração da música (`PITMUSIC.INI`)

```ini
[options]
log=0      ; 1 = grava PITMM.LOG (diagnóstico)
realcd=0   ; 1 = usa um drive de CD físico com o disco original

[tracks]
5=music\Track05.mp3   ; faixa do CD = arquivo, relativo à pasta do jogo
```

Faixas sem arquivo tocam silêncio pelo tempo da faixa original. A faixa usada em cada fase está em [docs/TECHNICAL.md](docs/TECHNICAL.md#track-usage).

## Compilação

Compilado com MinGW-w64 (32 bits), por exemplo no Ubuntu ou no WSL:

```sh
sudo apt install gcc-mingw-w64-i686 python3 zip
make          # build/WAIL32.DLL, build/PITMM.DLL, build/PitfallFix.exe
make dist VERSION=v1.0.0
```

O GitHub Actions compila a cada push e anexa o zip nas releases de tags `v*`.

## Próximos passos

* Tela cheia sem bordas com escala inteira / proporção correta (o jogo renderiza em 320×224)
* Filtros de escala opcionais
* Correção da animação de paleta em desktops de 32 bits
* Suporte a controles XInput

## Créditos

* Engenharia reversa e correções: Alessandro Ramos, com o Claude (Anthropic).
* Duração das faixas do CD: dump do disco de PC no [Redump](http://redump.org/).

*Pitfall: The Mayan Adventure* © 1995 Activision. Projeto de fã, não oficial e sem vínculo com a Activision. O código deste repositório é distribuído sob a [Licença MIT](LICENSE).
