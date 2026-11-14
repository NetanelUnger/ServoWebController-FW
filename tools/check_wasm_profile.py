"""Native tests for the actual bounded firmware preflight parser.
Pass a directory containing sensor/counter/hold/loop.wasm from the browser
compiler. Does not contact hardware or build the Web application.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from check_ble_transport import find_msvc_environment

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wasm/wasm_profile.c"
static unsigned rng=0x12abc89;
static unsigned random32(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(int argc,char **argv) {
    unsigned char bytes[4096],copy[4096];
    assert(!RobotWasm_Validate(NULL,8));
    for(int i=1;i<argc;i+=2) {
        FILE *file=fopen(argv[i+1],"rb");assert(file);
        unsigned n=(unsigned)fread(bytes,1,sizeof(bytes),file);fclose(file);
        int actual=RobotWasm_Validate(bytes,n);if(actual!=atoi(argv[i]))fprintf(stderr,"Unexpected profile result: %s (%d)\n",argv[i+1],actual);
        assert(actual==atoi(argv[i]));
        for(unsigned cut=0;cut<n;cut++) (void)RobotWasm_Validate(bytes,cut);
        for(unsigned trial=0;trial<20000;trial++) {
            memcpy(copy,bytes,n);
            for(unsigned j=0;j<4;j++)copy[random32()%n]=(unsigned char)random32();
            (void)RobotWasm_Validate(copy,n);
        }
    }
    for(unsigned trial=0;trial<20000;trial++) {
        unsigned n=8+random32()%2041;
        for(unsigned j=0;j<n;j++)bytes[j]=(unsigned char)random32();
        memcpy(bytes,"\0asm\1\0\0\0",8);(void)RobotWasm_Validate(bytes,n);
    }
    puts("PASS: browser modules, rejected profiles, every truncated prefix, deterministic mutation fuzz");
    return 0;
}
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--modules', type=Path, required=True)
    args = parser.parse_args()
    env = find_msvc_environment() if os.name == 'nt' else None
    cc = shutil.which('cl.exe', path=next(v for k,v in env.items() if k.upper() == 'PATH')) if env else shutil.which('cc')
    assert cc, 'Native C compiler required'
    with tempfile.TemporaryDirectory(prefix='wasm-profile-') as directory:
        folder = Path(directory); source = folder/'test.c'; source.write_text(HARNESS)
        exe = folder/'profile-test.exe'
        if env:
            command = [cc, '/nologo', '/std:c11', '/W4', '/I'+str(ROOT/'STM32CubeIDE/Application/User/Core'), str(source), '/Fe:'+str(exe), '/Fo:'+str(folder/'test.obj')]
        else:
            command = [cc, '-std=c11', '-Wall', '-Wextra', '-I'+str(ROOT/'STM32CubeIDE/Application/User/Core'), str(source), '-o', str(exe)]
        subprocess.run(command, check=True, env=env)
        cases = []
        for name in ('sensor','counter','hold','loop'):
            cases.extend(['1', str(args.modules.resolve()/(name+'.wasm'))])
        sensor = (args.modules/'sensor.wasm').read_bytes()
        rejected = {
            'bad-version': sensor[:4]+b'\x02'+sensor[5:],
            'unknown-import': sensor.replace(b'robot', b'other'),
            'wrong-signature': sensor.replace(b'\x60\x01\x7f\x01\x7f', b'\x60\x01\x7d\x01\x7f', 1),
            'memory-grow': sensor.replace(b'\x05\x04\x01\x01\x01\x01', b'\x05\x04\x01\x01\x01\x02'),
            'start': sensor+b'\x08\x01\x01',
            'oversized': sensor+bytes(2049),
        }
        for name, data in rejected.items():
            path = folder/(name+'.wasm'); path.write_bytes(data); cases.extend(['0',str(path)])
        subprocess.run([str(exe), *cases], check=True, env=env)

if __name__ == '__main__':
    main()
