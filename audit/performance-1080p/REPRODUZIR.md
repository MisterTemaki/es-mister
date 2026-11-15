# Compilação e reprodução da etapa final

Os comandos abaixo partem da pasta `es-mister-audit`, que contém `es-mister/`
e `validation/`. A bancada registrada usa WSL Ubuntu-26.04 e OpenSSH do Windows.
O endereço do aparelho e a chave pública autorizada estão nos scripts; a chave
privada não é distribuída. Use prefixos novos para não misturar execuções.

## Build distribuído

No Linux, o caminho oficial continua sendo `mister/build.sh`; a variante `es`
reutiliza dependências já preparadas, enquanto `all` também as prepara e empacota.
Zig 0.15.2, CMake, Ninja e as ferramentas listadas no cabeçalho do script são
necessários. O ELF é estático ARMv7/musl, Cortex-A9 com NEON.

```bash
cd es-mister
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install ziglang==0.15.2
export MISTER_WORK=/caminho/com-espaco
export JOBS=6
bash mister/build.sh deps
bash mister/build.sh es
```

O ELF fica em `$MISTER_WORK/out/emulationstation`. A atualização parcial utiliza
o Carbon já instalado. `mister/build.sh all` também gera um pacote completo,
incluindo a preparação de temas, e não é necessário para atualizar esse ELF.

O cache da sessão não faz parte da entrega. Com ele ainda disponível neste PC:

```powershell
wsl -d Ubuntu-26.04 -u root -- bash ./validation/cross_cache.sh bash ./validation/cross_build.sh reproducao-candidate active
wsl -d Ubuntu-26.04 -- arm-linux-gnueabihf-strip -o ./validation/arm/emulationstation-reproducao-candidate-stripped ./validation/arm/emulationstation-reproducao-candidate
wsl -d Ubuntu-26.04 -u root -- bash ./validation/cross_cache.sh bash ./validation/cross_build.sh reproducao-reference strict-reference
wsl -d Ubuntu-26.04 -- arm-linux-gnueabihf-strip -o ./validation/arm/emulationstation-reproducao-reference-stripped ./validation/arm/emulationstation-reproducao-reference
```

`strict-reference` restaura da referência verificada os fontes de catálogo,
recursos, rasterizador e saída. Os marcadores são comuns aos dois builds.
`tests/audit/reference/` permite reconstruir essa referência sem histórico Git.
O manifesto `*.source.json` registra cada arquivo preparado. Aguarde o término
da compilação antes de strip/upload. Um novo build pode ter hash diferente;
registre seu hash real em vez de reutilizar o da sessão anterior.

## Bancada no aparelho

O core deve estar em MENU. Não rode dois controladores de testes ao mesmo tempo.
Cada execução usa HOME isolado e a mesma configuração copiada da instalação.
A guarda de processos verifica o executável em `/proc`, aguarda também a saída
do wrapper anterior e recusa uma segunda instância. `restore` espera processo,
marcador `ready` e API, sem presumir que criar o wrapper tornou a UI pronta.

```powershell
wsl -d Ubuntu-26.04 -- python3 ./validation/target_run.py upload ./validation/arm/emulationstation-reproducao-reference-stripped stage3-final-reference
wsl -d Ubuntu-26.04 -- python3 ./validation/target_run.py upload ./validation/arm/emulationstation-reproducao-candidate-stripped stage3-final-candidate
```

Atualize os hashes esperados de `final_navigation.ps1`, `final_startup.ps1` e
`combined_compare.py` para os artefatos recém-compilados. A configuração medida
é 1920×1080, Carbon, duas threads de renderização, STRIP=8, PLD=256, FPS=60 e
OPTIMIZATIONS=1. A referência usa catálogo4/decoder2 e upload/fade/cópia novos
desligados; a candidata usa catálogo2/decoder1 e os três ligados. Não atribua à
alteração nova o ganho histórico contra OPTIMIZATIONS=0.

```powershell
pwsh -NoProfile -File ./validation/final_navigation.ps1 -Prefix reproducao-nav
pwsh -NoProfile -File ./validation/final_state_compare.ps1 -Mode idle -Prefix reproducao-idle
pwsh -NoProfile -File ./validation/final_state_compare.ps1 -Mode controls -Prefix reproducao-controls
pwsh -NoProfile -File ./validation/final_animation_compare.ps1 -Prefix reproducao-animations
pwsh -NoProfile -File ./validation/final_startup.ps1 -Prefix reproducao-warm
pwsh -NoProfile -File ./validation/final_startup.ps1 -Prefix reproducao-cold -Reboot
```

O último comando reinicia o Linux quatro vezes, confirma novo boot_id e MENU e
usa o mesmo intervalo de estabilização dos serviços em cada boot. Não executa
drop_caches nem lê o ELF antes da abertura medida. O hash é verificado depois.
A primeira tecla é injetada quando a UI chega ao marcador interativo, com
mudança real de seleção verificada; não se usa uma espera cega de nove segundos.

Os scripts `cover_compare.ps1`, `install_cover_fixture.py`, `cover_analyze.py`
reproduzem lista de 1.000 entradas, 160 PNGs originais compartilhados e ciclos
de ida/volta. O fixture não altera jogos, capas ou metadados reais. Os caminhos
dos fixtures precisam ser preparados no novo aparelho antes da execução.
`target_lifecycle.py core PREFIXO` usa somente `codex-video-probe.nes`, um
programa de teste original sem save RAM; não usa uma ROM comercial.

## Qualidade e testes no host

O host precisa de compilador C++17, CMake, pkg-config e headers/bibliotecas SDL2,
CURL e pugixml. Na sessão, pugixml veio do prefixo local `validation/tools/usr`;
esse diretório de dependências não está no ZIP. Em outro host use os pacotes de
desenvolvimento do sistema ou informe seu prefixo por CMAKE_PREFIX_PATH.

```bash
cmake -S es-mister/tests/audit -B validation/regressions -DAUDIT_SANITIZERS=ON
cmake --build validation/regressions -j2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./validation/regressions/audit_texture_manager
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./validation/regressions/audit_texture_manager ram
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./validation/regressions/audit_texture_manager ram-under-limit
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./validation/regressions/audit_regressions
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ./validation/regressions/audit_thread_pool
python3 validation/build_upload.py
python3 validation/check_target_payload.py
python3 validation/check_lifecycle_payload.py
```

Os diferenciais raster/backend usam a implementação anterior independente;
`tests/audit/raster_workload.cpp` inclui clipping, alfa, stencil, cores e UV.
Os scripts especializados de build/comparação estão em `validation/`; os logs
da execução aceita identificam quais testes rodaram no host e no ARM.
Protótipos em `tests/audit/experiments/` não entram no binário distribuído.

Para preparar um aparelho novo, gere as imagens determinísticas e instale o
fixture somente com os benchmarks parados. Estes comandos não fazem parte da
passagem de velocidade:

```bash
python3 validation/create_cover_fixture.py
python3 validation/install_cover_fixture.py
python3 validation/create_nes_probe.py
```

Velocidade e qualidade são passagens diferentes. Durante velocidade, não
habilite verificação dupla, PNG, slow dumps, perfil de sinais ou redesenho
forçado. Um binário de qualidade com relógio lógico fixo serve só para comparar
o mesmo estado da animação; esse código não está no main.cpp distribuído.

O limite de 60 FPS não comprova 60 redesenhos completos por segundo. Tempos
SDL/uinput terminam no primeiro redesenho escrito pelo software; não medem a
latência física do controle ou o scanout/TV, nem o fim de toda a transição.

No teste de retorno de core, os controles são enviados após uma espera de segurança
e validados por cinco probes de estado/seleção. Essa espera do roteiro não está no
launcher distribuído. O tempo de primeiro desenho após o helper é medido antes
dela; o menor tempo de entrada após retorno permanece não medido.
