# Auditoria es-mister — etapa 1

> Registro histórico da primeira etapa. A continuação com correções P01–P06,
> compilação ARM e medições reais está em [performance-1080p/RESULTADOS.md](performance-1080p/RESULTADOS.md).
> Os limites abaixo descrevem o estado daquela entrega e não substituem a atualização.

Data: 13/11/2026. Estado: correções desta etapa implementadas e validadas localmente; auditoria integral pendente.

**Material e base de comparação**

O ZIP fornecido contém 1.008 arquivos, 46.509.260 bytes descompactados e 434 arquivos textuais em `es-app/src` e `es-core/src`, com 168.365 linhas. O histórico Git e o conteúdo do submódulo `external/pugixml` não estão presentes. `5b5c7bc` é o identificador do nome recebido; não foi possível confirmá-lo contra o histórico original.

SHA-256 do ZIP original: `f87ee31502c598e3e6e5c526df0a2e770a8d7bf79ca9fb967ad329826669863a`.

O original foi preservado. A análise e as alterações ocorreram em uma cópia isolada, com commit local de referência. Nenhum outro projeto da pasta foi alterado. Documentos e comentários foram tratados como evidências do projeto, sem adotar instruções que desviassem do pedido.

**Arquitetura identificada**

| Componente | Responsabilidade, estado e fronteiras |
|---|---|
| `es-app/src/main.cpp` | Argumentos, inicialização assíncrona, janela, catálogo, input, serviços e loop principal. |
| `SystemData`, `FileData`, coleções e filtros | Sistemas configurados por XML, varredura de jogos e organização dos catálogos. `FileData::launchGame` libera recursos e executa o comando configurado. |
| `Gamelist`, `MetaData`, `Settings`, `SystemConf`, save states | Persistência XML/configuração e recuperação de metadados. Estes fluxos têm graus diferentes de tratamento de falhas. |
| `es-core` | Componentes gráficos, temas, fontes, texturas, entrada, áudio, recursos, utilitários e rede. |
| Renderizadores | OpenGL/GLES e backend por software. O backend por software grava comandos, desenha faixas com workers e copia regiões alteradas para uma saída em memória ou framebuffer Linux. |
| HTTP, scraping e instalação | `HttpReq` compartilha um multi handle libcurl protegido por mutex; callbacks recebem entradas da rede. `ZipFile` usa miniz e adapta nomes CP437/UTF-8. |
| Serviço HTTP local | Endpoints de consulta e mutação da aplicação. `PublicWebAccess` começa desabilitado; habilitá-lo amplia a fronteira de confiança. Ciclo de vida e endpoints ainda precisam de revisão detalhada. |
| MiSTer | Scripts geram `es_systems.cfg`, solicitam o console, iniciam ES e trocam cores por MGL e `/dev/MiSTer_cmd`. O helper usa framebuffer, consoles e uinput. `systems.tsv` define os sistemas. |
| Build e distribuição | CMake/C++17. `mister/build.sh` usa Zig/musl para ARMv7 Cortex-A9; `package.sh` reúne binário, helper, tema, controles e traduções. O port usa stb, software e stubs de VLC; não fornece HTTPS no build ARM atual. |

Invariantes preservadas: formatos XML/configurações existentes; comandos e tabela de sistemas; arquivos antigos durante falhas de download e salvamento; regras de nomes CP437/UTF-8; escolha explícita de destino no visualizador de ZIP; suporte condicionado por plataforma. A mudança de TLS passa a rejeitar certificados não confiáveis nos builds com HTTPS. Não foram alterados protocolos MiSTer, emuladores ou o formato MGL.

**Cobertura efetiva**

`cobertura.csv` e `cobertura.json` registram cada arquivo original, tamanho, hash antes/depois, status e profundidade. Há 16 arquivos analisados, 228 parcialmente analisados e 764 não analisados. Os arquivos novos de teste e documentação estão discriminados pelo diff, fora da contagem do original.

“Analisado” nesta etapa significa revisão manual do arquivo; não significa prova de ausência de bugs. A revisão detalhada cobre `HttpReq`, `ZipFile` e os 12 arquivos de `mister`. A persistência de configurações, o utilitário de arquivos, instalação, entrada, inicialização e concorrência receberam revisão dos fluxos indicados, sem alegar revisão integral desses arquivos.

As duas varreduras Cppcheck alcançaram respectivamente 208 unidades e 204 unidades do build com condicionais MiSTer. A análise automática de uma unidade não cobre necessariamente todas as configurações ou todos os seus cabeçalhos. Parte dos alertas revela limites de configuração da ferramenta, inclusive a expansão de `CURL_AT_LEAST_VERSION` em `HttpReq`; esse arquivo foi compilado e testado separadamente com sanitizers.

525 arquivos foram classificados como terceiros, recursos ou catálogos excluídos da revisão manual desta etapa: `external`, assets, traduções, `httplib.h`, miniz em `zip_file.hpp` e tabela `han.h`. Suas interfaces com arquivos, rede e decodificação continuam relevantes. Não foi realizada auditoria de CVEs, revisão integral dos parsers de mídia, fuzzing ou validação de todas as licenças de distribuição. As exclusões não são uma declaração de segurança dessas dependências.

**Plano executado e aceitação**

| Prioridade | Objetivo e componentes | Aceitação e validação | Risco considerado |
|---|---|---|---|
| 1 | Memória e confiança: callbacks HTTP, TLS e ZIP | Buffers com tamanho explícito, nomes convertidos com liberação correta, ZIP sem escape por caminhos, certificado não confiável rejeitado. Testes com ASan/UBSan/LSan. | Entradas malformadas; compatibilidade de nomes; TLS em outras plataformas. |
| 2 | Integridade: downloads, renomeação e Settings | Arquivo anterior preservado em erro HTTP, interrupção, falha de flush e falha de renomeação; Settings mantém alterações para nova tentativa e só dispara eventos após sucesso. | Falha de disco; diferenças Windows/POSIX; substituição interrompida. |
| 3 | Build: pugixml e traduções MiSTer | Compilação local do executável com `USE_SYSTEM_PUGIXML=ON`; MiSTer deixa de executar alvo gettext incompatível com seu leitor `.po`. | Submódulo ausente; ferramentas disponíveis no host; cross compilation não executada. |

**Achados corrigidos**

Todos os achados abaixo são confirmados por inspeção do código. A coluna de evidência distingue reprodução dinâmica de verificação por contrato ou compilação. Nenhum foi classificado como crítico; os de maior impacto foram classificados como altos nas condições indicadas.

| ID / severidade | Local, causa e condição | Impacto | Correção e evidência |
|---|---|---|---|
| A01 — alta | `HttpReq.cpp:565` original, `header_callback`: tratava buffer libcurl como string terminada por NUL. | Leitura fora dos limites ao receber um cabeçalho sem terminador. | String construída com o comprimento recebido. ASan reproduziu `heap-buffer-overflow` antes; teste `header` passa depois. O contrato confirma que o buffer não é terminado por NUL. [libcurl](https://curl.se/libcurl/c/CURLOPT_HEADERFUNCTION.html) |
| A02 — alta em LP64 | `HttpReq::status`: passava `int*` para `CURLINFO_RESPONSE_CODE`, que exige `long*`. | Escrita além da variável em plataformas com `long` de 64 bits; no ARM32 os tamanhos podem coincidir. | Variável alterada para `long`. Conferência do contrato, compilação e requisições locais; não se alega reprodução isolada dessa sobrescrita dentro da biblioteca não instrumentada. [Contrato](https://curl.se/libcurl/c/CURLINFO_RESPONSE_CODE.html) |
| A03 — alta | `HttpReq::performRequest`: `CURLOPT_SSL_VERIFYPEER=0` em toda requisição. | Servidores com certificados não confiáveis podiam entregar conteúdo ou receber credenciais nos builds com TLS. | Removida a desativação. Certificado autoassinado foi aceito antes e rejeitado depois; o teste com CA explicitamente confiada valida o caminho de sucesso. Não adiciona HTTPS ao port ARM. [Verificação TLS](https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYPEER.html) |
| A04 — alta | Download removia o destino na criação da requisição; `renameFile` removia o destino antes de tentar mover e informava sucesso para fonte ausente. | Perda do arquivo anterior por erro de rede ou falha de renomeação. | Destino mantido até a conclusão; renomeação com substituição nativa, sem fallback de cópia parcial. Testes de cancelamento, 404, resposta incompleta, substituição e falha real EXDEV passaram. |
| A05 — alta | `HttpReq::closeStream` ignorava falhas de `fflush` e `fclose`; callback não comparava escrita parcial. | Um download podia ser marcado como concluído após falha de disco no buffer final. | Erro de stream propagado; escrita parcial detectada. `/dev/full` reproduziu a falha antes e foi detectado depois. |
| A06 — média | Lista de `customHeaders` alocada com `curl_slist_append` ficava sem proprietário. | Vazamento em requisições repetidas, inclusive de scrapers autenticados. | Lista armazenada na requisição, tratada em falha de alocação e liberada no destrutor. Vinte ciclos executados sob LeakSanitizer. |
| A07 — baixa | Callback de escrita de arquivo retornava `nmemb`, em vez de bytes. | Violava o contrato quando `size != 1`; o caminho usual da libcurl usa tamanho 1. | Retorna o total de bytes; teste com `size=2`, `nmemb=3`. |
| A08 — média | `getContent` lia apenas o temporário, que já fora renomeado no sucesso. | Conteúdo vazio após um download concluído. | Lê o destino após sucesso e mantém o temporário nos demais estados; teste de download e leitura passou. |
| A09 — média | Cabeçalhos de respostas intermediárias permaneciam no mapa. | Metadados de redirecionamento podiam ser confundidos com os da resposta final. | Mapa limpo ao receber nova linha de status; teste `header-reset`. |
| A10 — alta | `ZipFile.cpp:119` original: `new[]` combinado com `delete` na conversão CP437. | Comportamento indefinido ao listar ZIPs sem flag UTF-8, incluindo nomes ASCII usuais. | `delete[]`; ASan reproduziu `alloc-dealloc-mismatch`, corrigido nos testes de listagem e extração. |
| A11 — média | `ZipFile::load` reutilizava mapa de nomes do arquivo anterior. | Falha ao extrair/calcular hash depois de trocar de ZIP com codificação diferente. | Estado reinicializado em todo load, inclusive falha de abertura; teste CP437 seguido de UTF-8 valida MD5 conhecido. |
| A12 — alta | `ZipFile::extract` concatenava nomes do ZIP; `ApiSystem::unzipFile` também criava diretórios sem validação. | Um ZIP malicioso podia escrever fora da pasta escolhida ou seguir um link já existente. | Validação de componentes e links antes da extração, inclusive de diretórios; criação dos pais de arquivos aninhados; falhas de extração propagadas. Testes cobrem `..`, caminhos absolutos, unidade Windows, barras invertidas e link para fora. No baseline os casos de ZIP abortavam primeiro pelo A10; a evidência independente de traversal é a inspeção do fluxo. |
| A13 — média | miniz usava leituras inteiras desalinhadas no x86. | UBSan identificou comportamento indefinido na leitura dos cabeçalhos. | Selecionado o caminho portátil de leitura byte a byte, sem editar o terceiro. Testes ZIP não geram diagnósticos com UBSan configurado para interromper. O ARM já usava esse caminho. |
| A14 — alta | `Settings::saveFile` ignorava `save_file`, limpava `mWasChanged` antes de gravar e anunciava sucesso. | Perda ou truncamento de configurações e impossibilidade de tentar novamente sem nova edição. | Gravação em temporário, conferência de flush/close, substituição por renomeação e limpeza da flag após sucesso. Eventos só depois do commit. Teste reproduziu sucesso incorreto antes; depois passou por falha, retry, preservação do anterior e XML válido. |
| A15 — média | `USE_SYSTEM_PUGIXML=ON` ainda deixava 16 includes apontando para o submódulo. | Build com biblioteca do sistema falhava, especialmente no ZIP que não contém o submódulo. | Cabeçalho `utils/Xml.h` seleciona a origem, preservando o caminho embarcado quando a opção está desligada. Executável completo compilou com pugixml 1.14 do sistema. O caminho com submódulo não foi compilado. |
| A16 — média | MiSTer incluía o alvo gettext, cujos caminhos usam `dir` como raiz de fontes embora nessa variante ele seja a pasta de build. O script depois descartava `locale/` com checkout. | Build dependia das ferramentas do host e podia descartar trabalho local em traduções. | Alvo gettext excluído em MiSTer, que já lê `.po`; removido o checkout. Compilação da variante e verificação sintática do script. Não foi executado o build ARM inteiro. |

**Resultados observados**

- Os 12 cenários iniciais falharam no baseline; quatro casos ZIP eram interrompidos pela mesma liberação incorreta de memória, portanto não representam quatro reproduções independentes.
- A suíte final HTTP/ZIP/arquivos tem 17 cenários, incluindo TLS com CA confiada. O resultado exato está em `evidencias/regressions-final.log`.
- A suíte de Settings passou: falha ao criar/substituir, nova tentativa, integridade do arquivo anterior, XML e emissão de eventos. Está em `evidencias/settings-final.log`.
- ASan, LSan e UBSan foram usados nas suítes isoladas. O executável completo foi compilado em Debug, sem instrumentação global por sanitizers.
- O executável local é **ELF x86-64**, com condicionais MiSTer. `--help`, inicialização, navegação para um jogo de teste e encerramento tiveram código zero. Capturas e log registram renderer em memória 640×360 e encerramento limpo. Não foi executada uma ROM.
- O ambiente sem placa de áudio gerou mensagens ALSA esperadas; a consulta a serviço Zaparoo indisponível gerou erro de conexão. Esses resultados não foram classificados como falhas do software corrigido.
- Cppcheck inicial: 930 alertas brutos. Cppcheck da configuração: 1.533 alertas brutos, incluindo 33 classificados pela ferramenta como `error`. Não são 33 bugs confirmados. Há repetição de cabeçalhos, configurações não suportadas e candidatos que precisam de reprodução. ShellCheck: 17 apontamentos, sem erros de sintaxe. Arquivos brutos foram preservados.
- Nenhum ganho de desempenho foi medido ou atribuído às alterações.

O smoke local registrou 18 quadros, 16 desenhados: rasterização média de **0,4 ms** e máxima de **1,3 ms**; tempo de quadro registrado de **31,2 ms** em média e pico de **403,7 ms** no teste curto, que inclui inicialização. A única tecla simulada teve resposta de **20 ms**. O teste estava limitado a 30 FPS, em Debug, x86-64/WSL, 640×360, saída em memória e catálogo artificial sem tema Carbon. Esses dados não são benchmark do MiSTer, não estabelecem FPS sustentado e não demonstram melhoria em relação ao original. O valor de percentil impresso pelo renderer não tem significado estatístico com uma única tecla.

**Pendências rastreáveis para as próximas etapas**

| ID / prioridade | Evidência e estado | Próxima validação / aceitação |
|---|---|---|
| P01 — alta | Confirmado por inspeção: `HttpServerThread::~HttpServerThread` destrói o servidor antes do join; o worker ainda pode usar esse objeto. O smoke não reproduziu falha. | Revisar propriedade e sincronização, testar encerramento durante requisições e início/fim imediato sob sanitizers. |
| P02 — alta | Confirmado por inspeção: `ThreadPool` compartilha flags bool sem sincronização; `wait()` permite saída de workers mantendo estado de execução. | Testar reuso, cancelamento, zero de `hardware_concurrency`, exceções e consumidores PDF/CBZ antes de mudar o contrato. |
| P03 — alta | Confirmado por inspeção: `copyFile` ignora erros de escrita/fechamento e retorna sucesso para fonte ausente. | Testar disco cheio, origem/destino iguais e consumidores de save states. Ainda não corrigido nesta etapa. |
| P04 — alta | Confirmado por inspeção dos consumidores: alguns instaladores ignoram retorno de unzip/rename e substituem conteúdo anterior antes de concluir todo o fluxo. | Instalação em staging, tratamento de falha e recuperação; testes de ZIP com CRC inválido e interrupção. Propagar erro no wrapper ZIP sozinho não torna esses instaladores transacionais. |
| P05 — média | Confirmado por inspeção: `jw_distance` usa dois vetores fixos de 65.000 inteiros na pilha e indexa conforme o tamanho das strings. O alerta específico de `s2[j]` permanece candidato, sem reprodução. | Testar metadados longos e preservar resultados de relevância antes de corrigir limites/armazenamento. |
| P06 — média | Risco confirmado de reprodutibilidade: RapidJSON e a origem de controles ainda usam `master`; submódulo pugixml ausente. Não há evidência de dependência comprometida. | Recuperar revisões exatas, registrar hashes e construir ARM de forma reproduzível. Sem atualizar dependências em bloco. |
| P07 — investigação | Alertas não confirmados em RetroAchievements, NetPlay, stb, miniz e outras áreas; revisão manual de grande parte da UI, scraping, imagens e configurações ainda pendente. | Reproduzir/descartar cada candidato, revisar arquivos restantes e atualizar a cobertura. |

**Limites e riscos residuais desta entrega**

Não houve teste no DE10-Nano: framebuffer, uinput, NEON, áudio real, hotplug, MGL e retorno do core precisam de validação no dispositivo. Windows e renderizadores OpenGL/GLES não foram compilados. O código Windows de renomeação foi revisado, sem execução nesse sistema.

A renomeação conserva o arquivo anterior nos erros de operação testados; não se afirma durabilidade diante de perda de energia, pois não foi adicionado protocolo de fsync. Temporários têm nomes fixos: concorrência sobre o mesmo destino continua fora do contrato testado. A proteção ZIP cobre nomes maliciosos e links preexistentes; não oferece isolamento contra um processo local que troque diretórios/links durante a extração. Não foram adicionados limites de tamanho total descompactado.

Os 764 arquivos ainda marcados “Não analisado” e as partes pendentes dos 228 arquivos parciais impedem declarar a auditoria integral concluída. As próximas etapas devem tratar os itens P01–P07 e o restante do inventário. Esta entrega fecha o incremento de HTTP, ZIP, configurações e build, preservando explicitamente as pendências.

**Compilar e reproduzir localmente**

Em Linux com C++17, CMake, Ninja, SDL2, SDL2_mixer, FreeType, libcurl, ALSA, libVLC (cabeçalhos), RapidJSON e pugixml instalados:

```sh
cmake -S . -B build-native -G Ninja -DMISTER=ON -DCEC=OFF \
  -DUSE_SYSTEM_PUGIXML=ON -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build-native -j2 --target emulationstation
cmake -S tests/audit -B build-audit -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-audit -j2
python3 tests/audit/run.py build-audit/audit_regressions
python3 tests/audit/run_settings.py build-audit/audit_settings
python3 tests/audit/smoke.py build-native/emulationstation --output-dir build-audit/smoke
```

`MISTER=ON` seleciona funcionalidades, não a arquitetura do compilador. **Não copie o ELF x86-64 de validação para o MiSTer.** Para ARM, o comando do projeto continua `bash mister/build.sh all`, depois de preparar Zig e as ferramentas documentadas no script e restaurar o submódulo pugixml da revisão apropriada. O ZIP não informa a revisão do submódulo; a compilação cruzada e o pacote de SD não foram produzidos nesta etapa.

Os testes não exigem migração de dados. Certificados inválidos agora são recusados; em serviços privados, instale a CA correta no ambiente com TLS. Os XMLs existentes mantêm seu formato.
