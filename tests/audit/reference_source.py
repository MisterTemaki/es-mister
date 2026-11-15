"""Verified immutable pre-change sources, including in source ZIPs without .git."""
import hashlib,json,pathlib,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[2]
REFERENCE=pathlib.Path(__file__).resolve().parent/'reference/cba2b66'
def read_reference(name):
    manifest=json.loads((REFERENCE/'manifest.json').read_text())
    if name not in manifest['files']:raise ValueError('Unknown reference file: '+name)
    try:data=subprocess.check_output(['git','-C',str(ROOT),'show',manifest['commit']+':'+name],stderr=subprocess.DEVNULL)
    except (subprocess.CalledProcessError,FileNotFoundError):data=(REFERENCE/name).read_bytes()
    if hashlib.sha256(data).hexdigest()!=manifest['files'][name]:raise ValueError('Reference hash mismatch: '+name)
    return data
