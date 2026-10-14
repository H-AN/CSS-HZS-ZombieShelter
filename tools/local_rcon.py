from pathlib import Path
import socket, struct,sys,json
ROOT=Path(r'D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server')
def packet(s,i,t,v):
    b=struct.pack('<ii',i,t)+v.encode()+b'\0\0';s.sendall(struct.pack('<i',len(b))+b)
def recv(s):
    def exact(n):
        b=b''
        while len(b)<n:
            v=s.recv(n-len(b))
            if not v:raise ConnectionError()
            b+=v
        return b
    n=struct.unpack('<i',exact(4))[0]; b=exact(n);return (*struct.unpack_from('<ii',b),b[8:-2].decode(errors='replace'))
def command(c):
    with socket.create_connection(('127.0.0.1',27129),timeout=3) as s:
        s.settimeout(.35);packet(s,1,3,(ROOT/'.zsh-local-rcon.txt').read_text().strip())
        while True:
            i,t,v=recv(s)
            if t==2:
                if i<0:raise RuntimeError('Authentication failed')
                break
        packet(s,2,2,c);out=[]
        try:
            while True:
                i,t,v=recv(s)
                if i==2:out.append(v)
        except (socket.timeout,ConnectionError):pass
        return ''.join(out)
if __name__=='__main__':
    sys.stdout.reconfigure(encoding='utf-8',errors='replace')
    for c in sys.argv[1:]:print(c,command(c))
