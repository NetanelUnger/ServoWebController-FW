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

def change_exports(data, change):
    """Rebuild only the export section of a compiler-produced test module."""
    def uint(at):
        result=shift=0
        while True:
            value=data[at]; at+=1; result|=(value&127)<<shift
            if not value&128:return result,at
            shift+=7
    def leb(value):
        result=bytearray()
        while value>=128:result.append((value&127)|128);value>>=7
        result.append(value);return bytes(result)
    at=8
    while at<len(data):
        begin=at;kind=data[at];size,at=uint(at+1);end=at+size
        if kind!=7:at=end;continue
        count,at=uint(at);exports=[]
        for _ in range(count):
            size,at=uint(at);name=data[at:at+size].decode();at+=size
            kind=data[at];index,at=uint(at+1);exports.append((name,kind,index))
        exports=change(exports)
        payload=leb(len(exports))+b''.join(leb(len(n))+n.encode()+bytes([k])+leb(i) for n,k,i in exports)
        return data[:begin]+b'\x07'+leb(len(payload))+payload+data[end:]
    raise AssertionError('Export section absent')

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wasm/wasm_profile.c"
#include "wasm/robot_sensor_math.h"
static unsigned rng=0x12abc89;
static unsigned random32(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(int argc,char **argv) {
    unsigned char bytes[4096],copy[4096];
    uint16_t map[64];for(unsigned i=0;i<64;i++)map[i]=UINT16_MAX;
    RobotTofStats stats=RobotTof_Reduce(map,true,0,65534);
    assert(stats.count==0 && stats.minimum==-1 && stats.mean==-1 && stats.min_zone==-1);
    map[0]=0;map[3]=100;map[4]=100;map[63]=200;
    stats=RobotTof_Reduce(map,true,100,200);
    assert(stats.count==3 && stats.minimum==100 && stats.maximum==200 && stats.mean==133 && stats.min_zone==3 && stats.max_zone==63);
    assert(RobotTof_Value(map,true,0,0,0)==0 && RobotTof_Value(map,true,64,0,65534)==-1);
    assert(RobotTof_Reduce(map,false,0,65534).count==0);
    assert(RobotTof_Reduce(map,true,101,199).count==0);
    for(unsigned i=0;i<64;i++)map[i]=65534;
    stats=RobotTof_Reduce(map,true,0,65534);assert(stats.count==64 && stats.mean==65534 && stats.max_zone==0);
    uint8_t motion[12]={0};motion[0]=0xe8;motion[1]=3;motion[6]=0x18;motion[7]=0xfc;
    assert(RobotImu_Axis(motion,true,0,false)==122 && RobotImu_Axis(motion,true,0,true)==-70000);
    assert(RobotImu_Axis(motion,false,0,false)==INT32_MIN && RobotImu_Axis(motion,true,3,false)==INT32_MIN);
    motion[9]=0x80;motion[10]=0xff;motion[11]=0x7f;
    assert(RobotImu_Axis(motion,true,1,true)==-2293760 && RobotImu_Axis(motion,true,2,true)==2293690);
    assert(RobotImu_Axis(motion,true,1,false)==0);
    assert(!RobotWasm_Validate(NULL,8));
    for(int i=1;i<argc;i+=2) {
        FILE *file=fopen(argv[i+1],"rb");assert(file);
        unsigned n=(unsigned)fread(bytes,1,sizeof(bytes),file);fclose(file);
        uint8_t abi=0;bool valid=RobotWasm_Inspect(bytes,n,&abi);int actual=valid?abi:0;
        if(actual!=atoi(argv[i]))fprintf(stderr,"Unexpected profile result: %s (%d)\n",argv[i+1],actual);
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
    puts("PASS: sensor reductions, inclusive ranges, empty/invalid maps, ties, units and IMU validity");
    return 0;
}
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--modules', type=Path, required=True)
    parser.add_argument('--abi2-modules', type=Path)
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
        if args.abi2_modules:
            for module in sorted(args.abi2_modules.glob('*.wasm')):cases.extend(['2',str(module.resolve())])
        sensor = (args.modules/'sensor.wasm').read_bytes()
        rejected = {
            'bad-version': sensor[:4]+b'\x02'+sensor[5:],
            'unknown-import': sensor.replace(b'robot', b'other'),
            'wrong-signature': sensor.replace(b'\x60\x01\x7f\x01\x7f', b'\x60\x01\x7d\x01\x7f', 1),
            'memory-grow': sensor.replace(b'\x05\x04\x01\x01\x01\x01', b'\x05\x04\x01\x01\x01\x02'),
            'start': sensor+b'\x08\x01\x01',
            'oversized': sensor+bytes(2049),
        }
        if args.abi2_modules:
            module=(args.abi2_modules/'init_once.wasm').read_bytes()
            rejected['init-without-marker']=change_exports(module,lambda rows:[r for r in rows if r[0]!='robot_abi_v2'])
            rejected['init-wrong-signature']=change_exports(module,lambda rows:[(n,k,next(i for n,k,i in rows if n=='step')) if n=='init' else (n,k,i) for n,k,i in rows])
            rejected['marker-wrong-signature']=change_exports(module,lambda rows:[(n,k,next(i for n,k,i in rows if n=='init')) if n=='robot_abi_v2' else (n,k,i) for n,k,i in rows])
            module=(args.abi2_modules/'idle.wasm').read_bytes()
            rejected['extended-import-without-marker']=change_exports(module,lambda rows:[r for r in rows if r[0] not in ('robot_abi_v2','init')])
        for name, data in rejected.items():
            path = folder/(name+'.wasm'); path.write_bytes(data); cases.extend(['0',str(path)])
        subprocess.run([str(exe), *cases], check=True, env=env)

if __name__ == '__main__':
    main()
