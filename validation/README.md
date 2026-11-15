# Reprodução dos testes e organização dos arquivos

Os scripts desta pasta são os utilizados durante a auditoria de 13–15/11/2026.
Conservam nomes, variantes históricas e caminhos da bancada. A release contém
os logs brutos, capturas e resultados, incluindo execuções invalidadas que são
identificadas no relatório. Os resumos JSON também estão em
`audit/performance-1080p/entrega-final/`.

## Preparação do diretório

Os scripts históricos esperam `es-mister/` e `validation/` como pastas irmãs.
Em PowerShell, partindo de uma pasta vazia:

```powershell
git clone --recurse-submodules https://github.com/MisterTemaki/es-mister.git es-mister-audit/es-mister
Copy-Item -LiteralPath './es-mister-audit/es-mister/validation' -Destination './es-mister-audit/validation' -Recurse
Set-Location './es-mister-audit'
```

Rode os comandos do [roteiro completo](../audit/performance-1080p/REPRODUZIR.md)
a partir dessa pasta `es-mister-audit`. O build oficial `mister/build.sh` pode
ser executado diretamente na raiz do repositório e não exige essa cópia.

A bancada original usa WSL Ubuntu-26.04, cache em `/mnt/es-mister-codex-arm`,
OpenSSH do Windows, MiSTer em `192.168.15.14` e o caminho local da chave
`id_ed25519_mister`. Ajuste endereços, caminhos, cache e hashes esperados antes
de usar outra máquina ou outro build. A chave privada não é distribuída.
`cross_cache.sh` pressupõe o cache preparado; uma máquina nova deve primeiro
preparar as dependências, conforme o roteiro de build. Nenhum cache está aqui.

Use somente um controlador de testes por vez, com o MiSTer em MENU e sem
outra instância do frontend. Os testes de abertura fria reiniciam o aparelho;
os testes de core lançam apenas a ROM original de diagnóstico incluída.
Leia o script escolhido antes de executar testes que instalem arquivos ou
controlem o aparelho. Scripts de empacotamento/relatório reproduzem o layout
da sessão e precisam dos artefatos respectivos, não incluídos no Git.

## Entradas principais

| Objetivo | Script |
|---|---|
| Navegação de 1.000 comandos por rodada | `final_navigation.ps1` |
| Primeiro comando após repouso e controles | `final_state_compare.ps1` |
| Abertura aquecida/fria | `final_startup.ps1` |
| Transições equivalentes | `final_animation_compare.ps1` |
| Lista com capas frias, ida e volta | `cover_compare.ps1` |
| Comparação de pixels do app | `upload_quality.ps1`, `png_compare.py` |
| Geração de capas originais | `create_cover_fixture.py` |
| Core de diagnóstico original | `create_nes_probe.py`, `target_lifecycle.py` |
| Conferência de payload sem SSH | `check_target_payload.py`, `check_lifecycle_payload.py` |
| Instalador e rollback em diretório temporário do host | `check_final_installer.py` |

`tests/audit/` contém os testes C++ e snapshots independentes da referência.
`arm/*.source.json` são manifestos de fontes, não binários. O ELF ARM aceito
está no ZIP de atualização da release e é identificado pelo SHA-256 no README.
O gerador da ROM `codex-video-probe.nes` e seu manifesto estão incluídos;
o programa de teste não tem save RAM e não é um jogo comercial.

Para medir velocidade, mantenha verificação dupla de pixels, PNG, slow dumps,
perfil de sinais e redesenho forçado desligados. A passagem de qualidade é
separada. FPS=60 não significa 60 redesenhos completos por segundo.
