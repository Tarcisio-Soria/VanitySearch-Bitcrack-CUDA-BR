# VanitySearch-Bitcrack-CUDA-BR

Versão **BR (PT-BR)** do [VanitySearch-Bitcrack](https://github.com/FixedPaul/VanitySearch-Bitcrack)
(de **FixedPaul**), ferramenta CUDA de busca de chaves privadas Bitcoin por faixa (estilo puzzle).
Fork **educacional**, com o mesmo painel ao vivo, telemetria e idioma do
[BitCrack-BR](https://github.com/Tarcisio-Soria/BitCrack-BR).

O README original do projeto-base foi preservado em [`README.upstream.md`](README.upstream.md).

---

## ⚠️ Aviso

Projeto de uso **educacional e de pesquisa**. A finalidade é estudar o custo de busca por força
bruta e resolver os **puzzles públicos** do Bitcoin (endereços criados de propósito para serem
encontrados). Varrer chaves de carteiras de terceiros é **inviável** (o espaço de 256 bits é
astronômico) e **ilegal**. Use apenas em endereços que são seus ou nos puzzles públicos.

## Novidades da versão BR

- 🖥️ **Painel ao vivo em duas fases** (redesenhado no mesmo lugar):
  - **Preparo** — GPU, VRAM, temperatura/clock/fan, configuração (threads × grupo), alvos,
    intervalo em bits, início/fim e **barra de progresso** na geração das chaves iniciais.
  - **Busca em andamento** — MKey/s, chaves varridas, barra de progresso + ETA, chave atual
    completa (64 hex), posição no espaço total (0 → 2²⁵⁶), chaves encontradas.
- 🌡️ **Telemetria NVML** (NVIDIA): temperatura colorida conforme o limite de *throttle* lido da
  própria GPU, clock e fan. VRAM usada/total também via NVML.
- 🇧🇷 **Mensagens e `--help` em português.**
- 🎯 **Multi-arquitetura**: SASS nativo para **sm_52, 61, 75, 80, 86, 89, 90 e 120**
  (Maxwell → Blackwell/RTX 50xx; `sm_120` exige CUDA ≥ 12.8).
- 🔧 Correções: velocidade não inicializada e divisão por zero no cálculo de progresso;
  build em compiladores recentes (gcc ≥ 14, `-include cstdint`).
- Mantido do projeto-base: pausa/retomada com **`p`** (libera a GPU), **`-backup`**, **`-random`**,
  `-range` em potência de 2.

> O arquivo de saída (`-o`) mantém o **formato original** (`Public Addr:` / `Priv (HEX):`) para não
> quebrar scripts; só o texto mostrado no terminal foi traduzido.

## Uso

```
vanitysearch [-v] [-gpuId N] [-i ARQUIVO] [-o ARQUIVO] [-start HEX] [-range BITS]
             [-m N] [-stop] [-random] [-backup] [ALVO]
```

| Opção | Descrição |
|---|---|
| `-start HEX` | chave privada inicial (hex) |
| `-range BITS` | varre de `start` até `start + 2^BITS` |
| `-i ARQUIVO` / `-o ARQUIVO` | lê alvos / grava resultados |
| `-gpuId N` | GPU a usar (apenas uma por execução) |
| `-random` | modo aleatório |
| `-backup` | retoma o progresso da última busca sequencial |
| `p` (durante a busca) | pausa/retoma e libera a GPU |

Apenas endereços/prefixos **comprimidos** e **uma GPU** por execução (limitações do projeto-base).

Exemplo (varre 2²⁸ chaves a partir de 1):

```
./vanitysearch -start 1 -range 28 ENDERECO
```

## Build (Linux)

```
make                       # build padrão (com NVML), compilador host g++-9
make CCBIN=g++-14          # escolhe o compilador host (necessário em distros recentes)
make NVML=0                # sem telemetria NVML (não exige libnvidia-ml)
make clean
```

- **CUDA Toolkit ≥ 12.8** para incluir `sm_120`. Para build mais rápido, deixe só a sua
  arquitetura em `GENCODE` no [`Makefile`](Makefile) (ex.: RTX 3060 = `sm_86`).
- Com NVML, o link usa os *stubs* de `$(CUDA)/lib64/stubs`; em execução a biblioteca vem do
  driver (`libnvidia-ml.so`). No WSL ela fica em `/usr/lib/wsl/lib`.
- Em distros muito recentes, veja as notas de glibc/gcc no README do
  [BitCrack-BR](https://github.com/Tarcisio-Soria/BitCrack-BR).

## Estrutura do que foi adicionado

| Arquivo | Papel |
|---|---|
| [`PainelBR.h`](PainelBR.h) / [`PainelBR.cpp`](PainelBR.cpp) | painel, barras, formatação e NVML — módulo independente do motor de busca |
| `Vanity.cpp` (`FindKeyGPU`, `PrintStats`, `getGPUStartingKeys`) | pontos de integração do painel |
| `main.cpp`, `Makefile` | ajuda em PT-BR, versão BR, multi-arch, NVML |

## Créditos

- **[Jean-Luc Pons](https://github.com/JeanLucPons/VanitySearch)** — autor do VanitySearch original.
- **[FixedPaul](https://github.com/FixedPaul/VanitySearch-Bitcrack)** — adaptação estilo BitCrack e
  otimizações CUDA (base deste fork).
- **Tarcisio Soria (KKND)** — adaptação BR: painel, telemetria, tradução e multi-arquitetura.

## Licença

**GNU Affero General Public License v3.0** (ou posterior) — veja [`LICENSE.txt`](LICENSE.txt).
Diferente do [BitCrack-BR](https://github.com/Tarcisio-Soria/BitCrack-BR) (MIT), este fork é
**copyleft** porque deriva de código AGPL; quem distribuir ou oferecer o programa em rede deve
disponibilizar o código-fonte, incluindo as modificações.
