"""Measure real native Supertonic French requests and the fixed textbook/answer UI on Windows."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import time
import psutil

ROOT=Path(__file__).resolve().parents[1]


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--run',type=Path,required=True)
    parser.add_argument('--repeat',type=int,default=3)
    args=parser.parse_args()
    args.run.mkdir(parents=True,exist_ok=False)
    exe=ROOT/'build/windows/langlern_tests.exe'
    env=os.environ.copy()
    env['PATH']=str(ROOT/'.deps/Qt/6.8.3/msvc2022_64/bin')+os.pathsep+env['PATH']
    env['LANGLERN_SPEECH_ROUNDS']='3'
    # Native requests, then the real textbook page/answer speech buttons (part of the OCR suite's real-page test).
    commands=[('requests',['speech','shortSentencesAndRequestReuse']),('textbook',['ocr','realTextbookOcrLookupAndSpeech'])]
    records=[]
    for index in range(args.repeat):
        directory=args.run/str(index+1);directory.mkdir()
        env['LANGLERN_SPEECH_ARTIFACT_DIR']=str(directory.resolve())
        record={'run':index+1,'requests':[],'processes':[]}
        for label,arguments in commands:
            log=directory/f'{label}.txt'
            started=time.monotonic_ns()
            with (directory/f'{label}-stderr.txt').open('wb') as stderr:
                process=subprocess.Popen([str(exe),*arguments,'-o',str(log)+',txt'],env=env,stdout=stderr,stderr=stderr)
                monitor=psutil.Process(process.pid);samples=[];modules=set()
                while process.poll() is None:
                    if (time.monotonic_ns()-started)>300*1e9:
                        process.kill();process.wait();raise TimeoutError('Speech benchmark exceeded 300 seconds')
                    try:
                        memory=monitor.memory_info();cpu=monitor.cpu_times()
                        samples.append({'elapsed_ms':(time.monotonic_ns()-started)/1e6,'rss_bytes':memory.rss,'private_bytes':memory.private,'cpu_seconds':cpu.user+cpu.system})
                        if len(samples)%50==0:modules.update(m.path for m in monitor.memory_maps())
                    except (psutil.NoSuchProcess,psutil.AccessDenied):
                        pass
                    time.sleep(.02)
                code=process.wait()
            (directory/f'{label}-memory.json').write_text(json.dumps(samples),encoding='utf-8')
            text=log.read_text(encoding='utf-8',errors='replace')
            for line in text.splitlines():
                if 'speech_short ' in line or 'speech_natural ' in line:
                    record['requests'].append({key:float(value) for key,value in re.findall(r'(\w+)=([\d.]+)',line)})
            record['processes'].append({'command':arguments,'exit_code':code,'skipped':'SKIP' in text or 'NOT executed' in text,
                                        'peak_rss_bytes':max((s['rss_bytes'] for s in samples),default=0),
                                        'peak_private_bytes':max((s['private_bytes'] for s in samples),default=0),
                                        'modules':sorted(modules)})
            if code:raise RuntimeError('Speech benchmark failed; retained '+str(directory))
        records.append(record)
    textbook=Path(env.get('LANGLERN_REAL_PDF','C:/Users/jiong/Desktop/法语/你好法语3学生用书第二版.pdf'))
    summary={'platform':platform.platform(),'cpu':platform.processor(),'logical_cpus':psutil.cpu_count(),
             'memory_bytes':psutil.virtual_memory().total,'exe_sha256':digest(exe),
             'model_lock_sha256':digest(ROOT/'third_party/speech.lock.json'),
             'textbook_sha256':digest(textbook) if textbook.exists() else None,
             'scope':'Release / OS caches warm; PCM availability proxy, no speaker presentation or GPU-memory measurement; includes Qt/PDF/OCR/UI resources',
             'results':records}
    (args.run/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
    print(json.dumps(summary,indent=2))


if __name__=='__main__':
    main()
