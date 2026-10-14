from pathlib import Path
import subprocess,socket,secrets,time,json,shutil,re,sys,os
import struct,argparse
def packet(sock,pid,kind,s):
    data=struct.pack('<ii',pid,kind)+s.encode()+b'\0\0'
    sock.sendall(struct.pack('<i',len(data))+data)
def recv(sock):
    def exact(n):
        result=b''
        while len(result)<n:
            chunk=sock.recv(n-len(result))
            if not chunk:raise ConnectionError('RCON closed')
            result+=chunk
        return result
    size=struct.unpack('<i',exact(4))[0]
    if size<10 or size>1048576:raise ValueError('Invalid RCON packet')
    body=exact(size);pid,kind=struct.unpack_from('<ii',body)
    return pid,kind,body[8:-2].decode(errors='replace')

parser=argparse.ArgumentParser(description='RCON tests for a prepared HZS shelter server')
parser.add_argument('--server-root',default=r'D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server')
parser.add_argument('--report-dir',default=str(Path(__file__).resolve().parents[1]/'tests'/'reports'))
parser.add_argument('--integration',action='store_true',help='Requires the test-only probe compiled as HanZombieShelter.smx')
parser.add_argument('--focused',action='store_true')
options=parser.parse_args()
CSS=Path(options.server_root);STAGE=CSS/'cstrike';PORT=27129;SERVER_CONFIG='zsh_codex_test.cfg'
REPORT=Path(options.report_dir);REPORT.mkdir(parents=True,exist_ok=True)
if not (STAGE/'cfg'/SERVER_CONFIG).is_file():raise SystemExit('Prepare the local server first; test cfg missing')
sys_encoding='utf-8'
sys.stdout.reconfigure(encoding='utf-8',errors='backslashreplace')
def run():
    availability=socket.socket(socket.AF_INET,socket.SOCK_STREAM)
    try:availability.bind(('127.0.0.1',PORT))
    finally:availability.close()
    secret=secrets.token_hex(12);log=STAGE/'shelter_runtime.log';results=[]
    history=REPORT/'早期联合调试';history.mkdir(exist_ok=True)
    for old in (STAGE/'addons'/'sourcemod'/'logs').glob('errors*'):
        assert old.resolve().is_relative_to(STAGE.resolve())
        target=history/(str(time.time_ns())+'_'+old.name);assert not target.exists();shutil.move(str(old),str(target))
    if log.exists():log.unlink()
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    proc=subprocess.Popen([str(CSS/'srcds_win64.exe'),'-game',str(STAGE),'-console','-insecure','-nohltv','-dev',
        '-ip','127.0.0.1','-port',str(PORT),'+maxplayers','32','+sv_lan','1','+sv_cheats','1','+servercfgfile',SERVER_CONFIG,
        '+rcon_password',secret,'+con_logfile','shelter_runtime.log','+map','zsh_desertplant_cso_v1_d','+bot_add_ct'],
        cwd=CSS,startupinfo=startup,creationflags=subprocess.CREATE_NEW_CONSOLE)
    sock=None;nextid=10;last_status=''
    def command(cmd):
        nonlocal nextid,last_status
        pid=nextid;nextid+=1;packet(sock,pid,2,cmd);messages=[]
        try:
            while True:
                current,_,message=recv(sock)
                if current==pid:messages.append(message)
        except socket.timeout:pass
        except ConnectionError:
            if cmd!='quit':raise
        output=''.join(messages);results.append({'command':cmd,'output':output})
        if cmd!='sm_zsh_status' or output!=last_status:
            print(cmd,output[:3000],flush=True)
        if cmd=='sm_zsh_status':last_status=output
        return output
    try:
        for _ in range(45):
            if proc.poll() is not None:raise RuntimeError('server exited before RCON')
            try:
                sock=socket.create_connection(('127.0.0.1',PORT),timeout=1);sock.settimeout(.5);packet(sock,1,3,secret)
                while True:
                    current,kind,_=recv(sock)
                    if kind==2:assert current!=-1;break
                break
            except (OSError,ConnectionError):
                if sock:sock.close();sock=None
                time.sleep(.5)
        assert sock
        command('sm plugins list');command('status');command('hzs_maxzombiespawn');command('bot_stop 1')
        if '--integration' in sys.argv:command('zsh_test_client')
        else:
            command('bot_join_after_player 0');command('bot_join_team ct');command('bot_quota 1')
            status=command('status')
            if '1 bots' not in status:command('bot_add_ct')
        day=''
        for attempt in range(24):
            time.sleep(1.0);day=command('sm_zsh_status')
            if 'phase=1' in day:break
        assert 'enabled=1' in day and 'phase=1' in day,'mode did not start'
        if '--integration' in sys.argv:command('zsh_test_bounds')
        assert 'bases=3/3' in day and 'nodes=32' in day and 'bosses=1' in day
        if '--integration' in sys.argv:
            if '--focused' in sys.argv:
                for case in ['gather','wall']:command('zsh_test '+case)
                summary=command('zsh_test summary')
                command('sm_zsh_fail');time.sleep(4.5);command('sm plugins unload UnmodifiedHZS');command('quit');proc.wait(timeout=15)
                assert 'Failed=0' in summary,'Focused assertion failed'
                return
            for case in ['world','gather','upgrade','placement','defenses','wall','boss','death']:
                command('zsh_test '+case)
            time.sleep(3.0);command('zsh_test reborn');command('zsh_test retention')
            for attempt in range(27):
                time.sleep(1.0);restored=command('sm_zsh_status')
                if 'phase=1' in restored and 'day=4' in restored:break
            else:raise AssertionError('campaign did not restart after real round failure')
            command('zsh_test restored');command('zsh_test wipe')
            for attempt in range(32):
                time.sleep(1.0);purged=command('sm_zsh_status')
                if 'phase=1' in purged and 'day=3' in purged:break
            else:raise AssertionError('actual core wipe did not restart the campaign')
            command('zsh_test purged');command('zsh_test victory');time.sleep(0.6)
            summary=command('zsh_test summary')
            assert 'Failed=0' in summary,'Integration assertion failed'
            time.sleep(4.0);reloaded=command('sm_zsh_status')
            assert 'phase=0' in reloaded and 'day=1' in reloaded and 'tracked=0' in reloaded,'boss outcome did not reload the configured next map'
            command('sm plugins unload UnmodifiedHZS');command('quit');proc.wait(timeout=15)
            return
        command('sm_zsh_phase night');time.sleep(4)
        night=command('sm_zsh_status');assert 'phase=2' in night
        assert re.search(r'raiders=[1-9]',night),'ordinary pool did not reactivate'
        command('sm_zsh_phase day');time.sleep(1)
        day_again=command('sm_zsh_status');assert 'raiders=0' in day_again and 'bosses=1' in day_again
        # HZS OnPluginEnd terminates a live round even during engine shutdown.
        # Stop the live round while GameRules still exists, without altering HZS source.
        command('sm_zsh_fail');time.sleep(4.5)
        command('sm plugins unload UnmodifiedHZS');command('quit');proc.wait(timeout=15)
    finally:
        if sock:sock.close()
        if proc.poll() is None:proc.terminate();proc.wait(timeout=5)
        if log.exists():shutil.copy2(log,REPORT/'原生主体联合服务端.txt')
        (REPORT/'原生主体联合RCON.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
        errors=[{'name':p.name,'text':p.read_text(encoding='utf-8',errors='replace')} for p in (STAGE/'addons'/'sourcemod'/'logs').glob('errors*')]
        (REPORT/'原生主体联合错误日志.json').write_text(json.dumps(errors,ensure_ascii=False,indent=2),encoding='utf-8')
        for error in errors:print('SM ERROR',error['text'][-7000:],flush=True)
        if errors:raise AssertionError('SourceMod runtime errors found')
    assert not errors,'SourceMod runtime errors found'
    print('Real unmodified HZS smoke passed',flush=True)
if __name__=='__main__':run()
