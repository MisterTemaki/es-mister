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
```

AddressSanitizer, LeakSanitizer e UndefinedBehaviorSanitizer ficam habilitados por padrão.
O cenário entre sistemas de arquivos executa quando `/dev/shm` existe e tem dispositivo diferente
do diretório temporário. O relatório registra que ele foi executado nesta máquina.

Para testar a interface compilada por software, sem framebuffer, com um catálogo artificial:

```sh
python3 tests/audit/smoke.py build-native/emulationstation --output-dir build-audit/smoke
```

Esse teste cria um link `resources` ao lado do binário quando ele não existe e salva logs/capturas
na pasta indicada. Ele valida ajuda, inicialização, navegação e encerramento. Não mede desempenho
do FPGA nem testa framebuffer, NEON, ALSA físico ou lançamento de cores.
