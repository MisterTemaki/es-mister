# Testes de regressão da auditoria

Testes locais em Linux, com C++17, CMake, libcurl, SDL2, pugixml e OpenSSL (comando).
Não acessam dispositivos do MiSTer nem executam jogos. Servidores HTTP/TLS usam apenas loopback;
arquivos são criados em diretórios temporários. Logging e a escolha de caminhos são isolados;
as implementações de HTTP, ZIP, strings, arquivos e configurações são as do projeto.

```sh
cmake -S tests/audit -B build-audit -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-audit -j2
python3 tests/audit/run.py build-audit/audit_regressions
python3 tests/audit/run_settings.py build-audit/audit_settings
python3 tests/audit/run_renderer.py build-audit/audit_renderer
build-audit/audit_http_server
build-audit/audit_thread_pool
build-audit/audit_copy_file
build-audit/audit_similarity
python3 tests/audit/run_theme.py build-audit/audit_theme
```

AddressSanitizer, LeakSanitizer e UndefinedBehaviorSanitizer ficam habilitados por padrão.
O cenário entre sistemas de arquivos executa quando `/dev/shm` existe e tem dispositivo diferente
do diretório temporário. O relatório registra que ele foi executado nesta máquina.

As suítes adicionais cobrem encerramento HTTP durante handler ativo e início imediato,
reuso/cancelamento/exceções do pool, falha real de escrita de cópia com preservação do
destino, equivalência da busca e metadados com 100.000 bytes, instalação de temas com
CRC inválido/traversal/rollback, e pixels do backend com comandos transparentes,
blend aditivo, stencil e recriação. A referência antiga de similaridade é apenas um
oráculo de teste para entradas limitadas, nunca código compilado na aplicação.

`raster_workload.cpp` permite comparação diferencial de 16 cenas com o rasterizador
anterior em x86 e ARM/NEON. O modo `bench` é sintético e não mede latência da TV.
As medições de navegação do DE10-Nano estão em `audit/performance-1080p/RESULTADOS.md`.

Para testar a interface compilada por software, sem framebuffer, com um catálogo artificial:

```sh
python3 tests/audit/smoke.py build-native/emulationstation --output-dir build-audit/smoke
```

Esse teste cria um link `resources` ao lado do binário quando ele não existe e salva logs/capturas
na pasta indicada. Ele valida ajuda, inicialização, navegação e encerramento. Não mede desempenho
do FPGA nem testa framebuffer, NEON, ALSA físico ou lançamento de cores.
