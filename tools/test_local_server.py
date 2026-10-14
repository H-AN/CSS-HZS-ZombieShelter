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

def classify_error_logs(errors):
    """Keep automatic-updater failures separate without hiding plugin errors.

    Only complete, explicitly tagged automatic-updater blocks are non-runtime
    findings. Unrecognized lines, stray updater lines, and unfinished blocks
    remain failures. Mixed plugin errors inside an updater block also fail.
    """
    updater_blocks=[];runtime_errors=[];session_lines=[]
    begin='[UPDATER] --- BEGIN ERRORS FROM AUTOMATIC UPDATER ---'
    end='[UPDATER] --- END ERRORS FROM AUTOMATIC UPDATER ---'
    for error in errors:
        pending=None
        for number,line in enumerate(error['text'].splitlines(),1):
            payload=re.sub(r'^L \d{2}/\d{2}/\d{4} - \d{2}:\d{2}:\d{2}: ','',line)
            entry={'file':error['name'],'line':number,'text':line}
            if not payload.strip():continue
            if payload in ('SourceMod error session started','Error log file session closed.') or re.fullmatch(r'Info \(map "[^"\r\n]*"\) \(file "[^"\r\n]*"\)',payload):
                session_lines.append(entry);continue
            if payload==begin:
                if pending is not None:
                    runtime_errors.extend(dict(item,reason='unfinished_updater_block') for item in pending)
                pending=[entry];continue
            if pending is not None and payload==end:
                pending.append(entry)
                updater_blocks.append({'file':error['name'],'start_line':pending[0]['line'],
                    'end_line':number,'category':'automatic_updater_failure',
                    'text':'\n'.join(item['text'] for item in pending)})
                pending=None;continue
            if pending is not None and payload.startswith('[UPDATER] '):
                pending.append(entry);continue
            runtime_errors.append(dict(entry,reason='unclassified_runtime_error'))
        if pending is not None:
            runtime_errors.extend(dict(item,reason='unfinished_updater_block') for item in pending)
    return {'schema_version':1,'source_log_count':len(errors),
        'runtime_check_passed':not runtime_errors,'runtime_error_line_count':len(runtime_errors),
        'updater_block_count':len(updater_blocks),'ignored_session_line_count':len(session_lines),
        'runtime_errors':runtime_errors,'automatic_updater_errors':updater_blocks,
        'session_metadata':session_lines}

def write_error_classification(report,errors):
    classification=classify_error_logs(errors)
    (report/'原生主体联合错误分类.json').write_text(json.dumps(classification,ensure_ascii=False,indent=2),encoding='utf-8')
    print('SM log classification: runtime_error_lines=%d automatic_updater_blocks=%d' %
        (classification['runtime_error_line_count'],classification['updater_block_count']),flush=True)
    return classification

parser=argparse.ArgumentParser(description='RCON tests for a prepared HZS shelter server')
parser.add_argument('--server-root',default=r'D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server')
parser.add_argument('--report-dir',default=str(Path(__file__).resolve().parents[1]/'tests'/'reports'))
parser.add_argument('--integration',action='store_true',help='Requires the test-only probe compiled as HanZombieShelter.smx')
parser.add_argument('--focused',action='store_true')
parser.add_argument('--classify-existing',action='store_true',help='Classify the existing error-log report offline; do not launch a server')
options=parser.parse_args()
CSS=Path(options.server_root);STAGE=CSS/'cstrike';PORT=27129;SERVER_CONFIG='zsh_codex_test.cfg'
REPORT=Path(options.report_dir);REPORT.mkdir(parents=True,exist_ok=True)
sys_encoding='utf-8'
sys.stdout.reconfigure(encoding='utf-8',errors='backslashreplace')
if options.classify_existing:
    existing=json.loads((REPORT/'原生主体联合错误日志.json').read_text(encoding='utf-8-sig'))
    classified=write_error_classification(REPORT,existing)
    raise SystemExit(0 if classified['runtime_check_passed'] else 1)
if not (STAGE/'cfg'/SERVER_CONFIG).is_file():raise SystemExit('Prepare the local server first; test cfg missing')

def configured_integer(path,key,fallback):
    text=path.read_text(encoding='utf-8-sig')
    match=re.search(r'"'+re.escape(key)+r'"\s+"(\d+)"',text)
    return int(match.group(1)) if match else fallback

MODE_CONFIG=STAGE/'addons'/'sourcemod'/'configs'/'HanZombieShelter'
NATIVE_POOL=configured_integer(MODE_CONFIG/'zombies.cfg','initial_ordinary_count',80)
DAY_TARGET=configured_integer(MODE_CONFIG/'days.cfg','day_target_alive',8)
NIGHT_TARGET=configured_integer(MODE_CONFIG/'days.cfg','night_target_alive',50)

def status_number(status,key):
    match=re.search(r'\b'+re.escape(key)+r'=(\d+)\b',status)
    return int(match.group(1)) if match else None
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
    assertions=STAGE/'addons/sourcemod/logs/shelter_probe_assertions.log'
    if assertions.exists():shutil.move(str(assertions),str(history/(str(time.time_ns())+'_probe_assertions.log')))
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
    def wait_status(predicate,description,timeout=75):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            if proc.poll() is not None:raise RuntimeError('server exited while '+description)
            state=command('sm_zsh_status')
            if predicate(state):return state
            time.sleep(1.0)
        raise AssertionError(description+' timed out; last state: '+state)
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
        if options.integration:command('zsh_test_client')
        else:
            command('bot_join_after_player 0');command('bot_join_team ct');command('bot_quota 1')
            status=command('status')
            if '1 bots' not in status:command('bot_add_ct')
        day=wait_status(lambda state: status_number(state,'enabled')==1 and status_number(state,'phase')==1
            and status_number(state,'raiders')==DAY_TARGET and status_number(state,'tracked')==NATIVE_POOL+1,
            'initial native startup and daytime population',timeout=90)
        if options.integration:command('zsh_test_bounds')
        assert 'bases=3/3' in day and 'nodes=32' in day and 'bosses=1' in day
        if options.integration:
            if options.focused:
                for case in ['gather','inventory','wall']:command('zsh_test '+case)
                summary=command('zsh_test summary')
                command('sm_zsh_fail');time.sleep(4.5);command('sm plugins unload UnmodifiedHZS');command('quit');proc.wait(timeout=15)
                assert 'Failed=0' in summary,'Focused assertion failed'
                return
            command('zsh_test world')
            command('zsh_test progression')
            command('zsh_test growth')
            command('zsh_test resource_reroll')
            command('zsh_test day_population')
            # Test actual admitted NPC counts at each shipped difficulty tier,
            # not just that hzs_maxzombiespawn accepted a changed number.
            for stage_day, stage_target in [(2,80),(5,90),(10,100)]:
                command('sm_zsh_day '+str(stage_day));command('sm_zsh_phase night')
                populated=wait_status(lambda state: status_number(state,'phase')==2
                    and status_number(state,'day')==stage_day
                    and status_number(state,'raiders')==stage_target,
                    'day %d reaches %d active native ordinary NPCs' % (stage_day,stage_target),timeout=45)
                assert status_number(populated,'tracked')==NATIVE_POOL+1
                assert status_number(populated,'bosses')==1
                cap=command('hzs_maxzombiespawn')
                assert re.search(r'"hzs_maxzombiespawn"\s*=\s*"'+str(stage_target)+'"',cap),cap
            command('sm_zsh_day 1')
            command('sm_zsh_phase night')
            wait_status(lambda state: status_number(state,'phase')==2 and status_number(state,'raiders')==NIGHT_TARGET,
                'night population reaches configured target',timeout=40)
            command('zsh_test night_population');command('sm_zsh_phase day')
            wait_status(lambda state: status_number(state,'phase')==1 and status_number(state,'raiders')==DAY_TARGET,
                'dawn retains a small population',timeout=20)
            command('zsh_test dawn_population')
            for case in ['gather','inventory','safety_economy','menus','admin_menus','upgrade','placement','defenses','wall','boss','death']:
                command('zsh_test '+case)
            # Wait for both the native death-animation/reborn lifecycle and external admission.
            time.sleep(3.0)
            wait_status(lambda state: status_number(state,'phase')==1 and status_number(state,'tracked')==NATIVE_POOL+1
                and status_number(state,'raiders')==DAY_TARGET,'daytime ordinary death and native replacement',timeout=25)
            command('zsh_test reborn');command('zsh_test reset_day')
            wait_status(lambda state: status_number(state,'phase')==1 and status_number(state,'day')==1
                and status_number(state,'bosses')==1,'administrator reset of the current day')
            command('zsh_test reset_restored');command('zsh_test retention')
            wait_status(lambda state: status_number(state,'phase')==1 and status_number(state,'day')==4
                and status_number(state,'bosses')==1,'campaign restart after real round failure')
            command('zsh_test restored');command('zsh_test wipe')
            wait_status(lambda state: status_number(state,'phase')==1 and status_number(state,'day')==3
                and status_number(state,'bosses')==1,'actual core all-dead campaign restart')
            command('zsh_test purged');command('zsh_test victory');time.sleep(0.6)
            summary=command('zsh_test summary')
            assert 'Failed=0' in summary,'Integration assertion failed'
            time.sleep(4.0);reloaded=command('sm_zsh_status')
            assert 'phase=0' in reloaded and 'day=1' in reloaded and 'tracked=0' in reloaded,'boss outcome did not reload the configured next map'
            command('sm plugins unload UnmodifiedHZS');command('quit');proc.wait(timeout=15)
            return
        command('sm_zsh_phase night')
        wait_status(lambda state: status_number(state,'phase')==2 and status_number(state,'raiders')==NIGHT_TARGET,
            'production nighttime ordinary population',timeout=40)
        command('sm_zsh_phase day')
        day_again=wait_status(lambda state: status_number(state,'phase')==1 and status_number(state,'raiders')==DAY_TARGET,
            'production daytime ordinary population',timeout=20)
        assert 'bosses=1' in day_again
        # HZS OnPluginEnd terminates a live round even during engine shutdown.
        # Stop the live round while GameRules still exists, without altering HZS source.
        command('sm_zsh_fail');time.sleep(4.5)
        command('sm plugins unload UnmodifiedHZS');command('quit');proc.wait(timeout=15)
    finally:
        if sock:sock.close()
        if proc.poll() is None:proc.terminate();proc.wait(timeout=5)
        if assertions.exists():shutil.copy2(assertions,REPORT/'probe_assertions.log')
        if log.exists():shutil.copy2(log,REPORT/'原生主体联合服务端.txt')
        (REPORT/'原生主体联合RCON.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
        errors=[{'name':p.name,'text':p.read_text(encoding='utf-8',errors='replace')} for p in (STAGE/'addons'/'sourcemod'/'logs').glob('errors*')]
        (REPORT/'原生主体联合错误日志.json').write_text(json.dumps(errors,ensure_ascii=False,indent=2),encoding='utf-8')
        classified=write_error_classification(REPORT,errors)
        for issue in classified['automatic_updater_errors']:print('SM AUTOMATIC UPDATER',issue['text'],flush=True)
        for issue in classified['runtime_errors']:print('SM RUNTIME ERROR',issue['file'],issue['line'],issue['text'],flush=True)
        if not classified['runtime_check_passed']:raise AssertionError('SourceMod runtime errors found')
    assert classified['runtime_check_passed'],'SourceMod runtime errors found'
    print('Real unmodified HZS smoke passed',flush=True)
if __name__=='__main__':run()
