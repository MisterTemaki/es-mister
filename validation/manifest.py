import pathlib, zipfile, hashlib, csv, json, collections
base=pathlib.Path(__file__).resolve().parent
root=base.parent/'es-mister'
archive=pathlib.Path('/mnt/d/FPGA/Frontend/Mistercera/es-mister-src-5b5c7bc.zip')
output=root/'audit'
output.mkdir(exist_ok=True)
detailed={'es-core/src/HttpReq.cpp','es-core/src/HttpReq.h','es-core/src/utils/ZipFile.cpp','es-core/src/utils/ZipFile.h'}
partial={'CMakeLists.txt','es-app/CMakeLists.txt','es-core/CMakeLists.txt','external/CMakeLists.txt',
         'README.md','DEVNOTES.md','.gitmodules','locale/CMakeLists.txt','locale/checkLocale.sh','getPoFromWebsite.sh',
         'emulationstation.sh','resources/checkWheelGunGamesResources.py',
         'es-app/src/main.cpp','es-app/src/FileData.cpp','es-app/src/Gamelist.cpp','es-app/src/ApiSystem.cpp',
         'es-app/src/SaveState.cpp','es-app/src/ContentInstaller.cpp','es-app/src/ContentInstaller.h',
         'es-app/src/services/HttpServerThread.cpp','es-app/src/services/HttpServerThread.h',
         'es-app/src/services/HttpApi.h','es-app/src/views/SystemView.cpp',
         'es-app/src/views/ViewController.cpp','es-app/src/guis/GuiImageViewer.cpp',
         'es-core/src/Settings.cpp','es-core/src/Settings.h','es-core/src/Paths.cpp','es-core/src/Paths.h',
         'es-core/src/Log.cpp','es-core/src/Log.h','es-core/src/utils/ThreadPool.cpp','es-core/src/utils/ThreadPool.h',
         'es-core/src/utils/FileSystemUtil.cpp','es-core/src/utils/FileSystemUtil.h','es-core/src/utils/StringUtil.cpp',
         'es-core/src/resources/TextureDataManager.cpp','es-core/src/resources/TextureDataManager.h',
         'es-core/src/resources/TextureData.h','es-core/src/resources/ResourceManager.cpp',
         'es-core/src/renderers/Renderer_Soft.cpp','es-core/src/renderers/soft/SoftOutput.cpp',
         'es-core/src/renderers/soft/SoftScript.cpp','es-core/src/renderers/soft/SoftRaster.h'}
text_ext={'.cpp','.c','.h','.hpp','.cmake','.txt','.md','.sh','.py','.tsv','.xml','.cfg','.ini','.po','.pot','.json','.yml','.yaml','.in','.rc','.glsl','.vert','.frag','.svg'}
rows=[]
with zipfile.ZipFile(archive) as original:
    for item in original.infolist():
        if item.is_dir(): continue
        name=item.filename.removeprefix('es-mister/')
        before=original.read(item)
        path=root/name
        after=path.read_bytes()
        text=path.suffix.lower() in text_ext or path.name in ('CMakeLists.txt','.gitmodules','.gitignore')
        excluded=name.startswith(('external/','resources/','locale/lang/')) or name in ('es-app/src/services/httplib.h','es-core/src/utils/zip_file.hpp','es-core/src/utils/han.h')
        status='Não analisado'
        depth='Pendente de revisão manual.'
        if excluded:
            depth='Excluído da revisão manual desta etapa: terceiro, recurso ou catálogo; interfaces e riscos descritos no relatório.'
        if name.startswith('mister/') or name in detailed:
            status='Analisado'
            depth='Revisão manual do arquivo; testes locais nas interfaces corrigidas; hardware não validado.'
        elif name in partial:
            status='Parcialmente analisado'
            depth='Revisão manual dos fluxos ou interfaces descritos no relatório; restante pendente.'
        elif name.startswith(('es-app/src/','es-core/src/')) and path.suffix in ('.cpp','.c'):
            status='Parcialmente analisado'
            depth='Triagem estática Cppcheck; não equivale a revisão manual nem cobre todas as variantes.'
        rows.append(dict(arquivo=name,subsistema=name.split('/')[0],bytes_original=len(before),
                         linhas_original=before.count(b'\n') if text else '',
                         status=status,profundidade=depth,excluido_etapa=excluded,
                         alterado=before!=after,sha256_original=hashlib.sha256(before).hexdigest(),
                         sha256_atual=hashlib.sha256(after).hexdigest()))
rows.sort(key=lambda row:row['arquivo'])
with (output/'cobertura.csv').open('w',newline='',encoding='utf-8-sig') as file:
    writer=csv.DictWriter(file,fieldnames=rows[0].keys()); writer.writeheader(); writer.writerows(rows)
(output/'cobertura.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf8')
summary=dict(arquivos_originais=len(rows),bytes_originais=sum(r['bytes_original'] for r in rows),
             status=dict(collections.Counter(r['status'] for r in rows)),
             fontes_principais=sum(isinstance(r['linhas_original'],int) and r['arquivo'].startswith(('es-app/src/','es-core/src/')) for r in rows),
             linhas_fontes_principais=sum(r['linhas_original'] for r in rows if isinstance(r['linhas_original'],int) and r['arquivo'].startswith(('es-app/src/','es-core/src/'))),
             sha256_zip=hashlib.sha256(archive.read_bytes()).hexdigest())
(output/'inventario-resumo.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(summary,ensure_ascii=False,indent=2))
