import pathlib
base=pathlib.Path(__file__).resolve().parent.parent/'es-mister'
file=base/'es-app/src/FileFilterIndex.cpp'
text=file.read_text()
start=text.index('float jw_distance(');body=text.index('{',start)
depth=1;end=body+1
while depth:
    if text[end]=='{':depth+=1
    elif text[end]=='}':depth-=1
    end+=1
reference=text[start:end].replace('float jw_distance(','float referenceJaro(',1)
gold=base/'tests/audit/similarity_reference.inc'
if not gold.exists():gold.write_text('// Original function, test oracle for bounded ASCII inputs only.\n'+reference+'\n')
new='float jw_distance(std::string s1, std::string s2, bool caseSensitive = true) {\n\treturn Utils::String::jaroWinkler(std::move(s1), std::move(s2), caseSensitive);\n}'
text=text[:start]+new+text[end:]
if '#include "utils/Similarity.h"' not in text:text=text.replace('#include "utils/StringUtil.h"','#include "utils/StringUtil.h"\n#include "utils/Similarity.h"',1)
file.write_text(text)
