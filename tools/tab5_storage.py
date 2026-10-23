#!/usr/bin/env python3
"""Tab5 storage access using OpenSSH and the standard-library SFTP v3 client."""
import argparse
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import shlex
import stat
import struct
import subprocess
import sys
import tempfile

ROOTS = ('/sd', '/usb', '/usb1', '/usb2', '/usb3')
MAX_PACKET = 1024 * 1024
CHUNK = 16384

class StorageError(Exception):
    pass

class StatusError(StorageError):
    def __init__(self, code, message):
        names = {1:'end of file',2:'no such file',3:'permission denied',4:'failure',5:'bad message',6:'not connected',7:'connection lost',8:'operation unsupported'}
        self.code = code
        super().__init__('SFTP {}: {}'.format(names.get(code, str(code)), message))

def u32(n):
    return struct.pack('>I', n)

def string(value):
    if isinstance(value, str):
        value = value.encode('utf-8')
    return u32(len(value)) + value

class Reader:
    def __init__(self, data):
        self.data, self.offset = data, 0
    def take(self, count):
        if count < 0 or count > len(self.data) - self.offset:
            raise StorageError('truncated SFTP response')
        value = self.data[self.offset:self.offset+count]
        self.offset += count
        return value
    def uint(self):
        return struct.unpack('>I', self.take(4))[0]
    def string(self):
        return self.take(self.uint())
    def end(self):
        if self.offset != len(self.data):
            raise StorageError('unexpected trailing SFTP data')
    def attrs(self):
        flags = self.uint()
        if flags & ~0x8000000f:
            raise StorageError('unknown SFTP attribute flags')
        for mask, count in ((1,8),(2,8),(4,4),(8,8)):
            if flags & mask:
                self.take(count)
        if flags & 0x80000000:
            count = self.uint()
            if count > 4096:
                raise StorageError('too many SFTP extended attributes')
            for _ in range(count):
                self.string(); self.string()

class SFTP:
    def __init__(self, incoming, outgoing):
        self.incoming, self.outgoing, self.sequence = incoming, outgoing, 0
        self.send(1, u32(3))
        kind, data = self.receive()
        if kind != 2:
            raise StorageError('SFTP VERSION response required')
        r = Reader(data)
        if r.uint() != 3:
            raise StorageError('server does not provide SFTP v3')
        while r.offset < len(r.data):
            r.string(); r.string()  # optional extension name/data
    def read_exact(self, count):
        parts = bytearray()
        while len(parts) < count:
            chunk = self.incoming.read(count-len(parts))
            if not chunk:
                raise StorageError('SSH/SFTP connection closed during response')
            parts.extend(chunk)
        return bytes(parts)
    def send(self, kind, body):
        packet = bytes([kind]) + body
        if len(packet) > MAX_PACKET:
            raise StorageError('SFTP request too large')
        self.outgoing.write(u32(len(packet)) + packet)
        self.outgoing.flush()
    def receive(self):
        size = struct.unpack('>I', self.read_exact(4))[0]
        if size < 1 or size > MAX_PACKET:
            raise StorageError('invalid SFTP packet length')
        packet = self.read_exact(size)
        return packet[0], packet[1:]
    def request(self, kind, body, expected, eof=False):
        self.sequence += 1
        self.send(kind, u32(self.sequence)+body)
        reply, data = self.receive()
        r = Reader(data)
        if r.uint() != self.sequence:
            raise StorageError('SFTP response ID mismatch')
        if reply == 101:
            code = r.uint()
            message = r.string().decode('utf-8', 'replace')
            r.string(); r.end()
            if code == 1 and eof:
                return None
            if code or expected != 101:
                raise StatusError(code, message)
            return b''
        if reply != expected:
            raise StorageError('unexpected SFTP response type {}'.format(reply))
        return r.take(len(r.data)-r.offset)
    def open(self, path, flags):
        r = Reader(self.request(3, string(path)+u32(flags)+u32(0), 102))
        handle = r.string(); r.end()
        if not handle or len(handle) > 256:
            raise StorageError('invalid SFTP handle')
        return handle
    def close(self, handle):
        self.request(4, string(handle), 101)
    def close_quiet(self, handle):
        try:
            self.close(handle)
        except (StorageError, OSError):
            pass
    def names(self, path):
        r = Reader(self.request(11, string(path), 102))
        handle = r.string(); r.end()
        try:
            while True:
                data = self.request(12, string(handle), 104, eof=True)
                if data is None:
                    break
                r = Reader(data); count = r.uint()
                if not count or count > 4096:
                    raise StorageError('invalid SFTP directory entry count')
                for _ in range(count):
                    name = r.string().decode('utf-8','replace')
                    r.string(); r.attrs()
                    yield name
                r.end()
        finally:
            self.close_quiet(handle)
    def download(self, remote, output):
        handle = self.open(remote, 1)
        offset = 0
        try:
            while True:
                data = self.request(5, string(handle)+struct.pack('>Q',offset)+u32(CHUNK), 103, eof=True)
                if data is None:
                    break
                r = Reader(data); chunk = r.string(); r.end()
                if not chunk or len(chunk) > CHUNK:
                    raise StorageError('invalid SFTP READ data length')
                output.write(chunk); offset += len(chunk)
        except BaseException:
            self.close_quiet(handle)
            raise
        self.close(handle)
        return offset
    def upload(self, source, remote, overwrite=False):
        # EXCL is atomic on the server; do not rely on a separate existence check.
        handle = self.open(remote, 2|8|(16 if overwrite else 32))
        offset = 0
        try:
            while True:
                chunk = source.read(CHUNK)
                if not chunk:
                    break
                self.request(6,string(handle)+struct.pack('>Q',offset)+string(chunk),101)
                offset += len(chunk)
        except BaseException:
            self.close_quiet(handle)
            raise
        self.close(handle)
        return offset
    def mkdir(self, remote):
        self.request(14,string(remote)+u32(0),101)
    def remove(self, remote):
        self.request(13,string(remote),101)

def remote_path(value, root_only=False):
    if '\x00' in value or not value.startswith('/') or '..' in value.split('/'):
        raise argparse.ArgumentTypeError('remote path must be absolute without .. or NUL')
    path = str(PurePosixPath(value))
    if not any(path == root or (not root_only and path.startswith(root+'/')) for root in ROOTS):
        raise argparse.ArgumentTypeError('storage roots: '+', '.join(ROOTS))
    return path

def identity(value):
    if not value or value.startswith('-') or not re.fullmatch(r'[A-Za-z0-9_.:@\[\]-]+',value):
        raise argparse.ArgumentTypeError('invalid SSH host/user (leading - is forbidden)')
    return value

def port_number(value):
    try:
        port = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError('port must be 1..65535')
    if not 1 <= port <= 65535:
        raise argparse.ArgumentTypeError('port must be 1..65535')
    return port

def ssh_command(args):
    cmd = ['ssh','-T','-o','ConnectTimeout=10','-p',str(args.port)]
    if args.user:
        if '@' in args.host or '@' in args.user or ':' in args.user:
            raise StorageError('use --user or user@HOST, not both; username must be plain')
        cmd += ['-l',args.user]
    return cmd+['-s',args.host,'sftp']

def get_file(client, remote, destination):
    dest = Path(destination)
    # link() publishes without replacing an existing destination, including symlinks.
    with tempfile.NamedTemporaryFile(prefix='.tab5-get-',dir=dest.parent,delete=False) as output:
        temporary = Path(output.name)
        try:
            count = client.download(remote,output)
            output.flush(); os.fsync(output.fileno())
        except BaseException:
            output.close(); temporary.unlink()
            raise
    try:
        os.link(temporary,dest)
    finally:
        temporary.unlink()
    return count

def doctor():
    print('Python:',sys.version.split()[0])
    for name in ('ssh','sshfs','fusermount3','fusermount'):
        executable = shutil.which(name)
        print(name+':',executable or 'not found')
        if executable and name.startswith('fusermount'):
            info = os.stat(executable)
            print('  mode={} executable={} setuid={}'.format(stat.filemode(info.st_mode),os.access(executable,os.X_OK),bool(info.st_mode & stat.S_ISUID)))
    fuse = Path('/dev/fuse')
    print('/dev/fuse:', 'read/write allowed' if os.access(fuse,os.R_OK|os.W_OK) else 'missing or inaccessible')
    print('Mount needs sshfs, usable /dev/fuse and mount permissions; no sudo/install is performed.')
    print('Fallback: ls/get/put/mkdir/rm need only Python and OpenSSH; run ssh HOST to establish normal host-key/password trust.')
    return 0

def mount(args):
    sshfs = shutil.which('sshfs')
    if not sshfs or not os.access('/dev/fuse',os.R_OK|os.W_OK):
        raise StorageError('mount unavailable: install/configure sshfs and FUSE separately; use get/put/ls for transfer')
    point = Path(args.mountpoint).resolve()
    if not point.is_dir() or any(point.iterdir()):
        raise StorageError('mountpoint must be an existing empty directory')
    host = args.host
    if args.user:
        if '@' in host or '@' in args.user or ':' in args.user:
            raise StorageError('use --user or user@HOST, not both')
        host = args.user+'@'+host
    cmd = [sshfs,'-f','-p',str(args.port),'-o','rw' if args.rw else 'ro','-o','max_write=32768','-o','max_conns=1',host+':'+args.root,str(point)]
    print('Foreground mount ({}); Ctrl-C to stop. Unmount with fusermount3 -u MOUNTPOINT.'.format('read/write' if args.rw else 'read-only'),flush=True)
    return subprocess.call(cmd)

def mount_ready():
    return bool(shutil.which('sshfs') and
                (shutil.which('fusermount3') or shutil.which('fusermount')) and
                os.access('/dev/fuse',os.R_OK|os.W_OK))

def transfer_prompt(client,args):
    print('Transfer mode: no FUSE mount. Commands: ls [path], get REMOTE LOCAL, '
          'put LOCAL REMOTE [--overwrite], mkdir PATH, rm PATH, quit.')
    def path(value):
        remote=remote_path(value if value.startswith('/') else args.root+'/'+value)
        if remote!=args.root and not remote.startswith(args.root+'/'):
            raise StorageError('path is outside the selected root')
        return remote
    while True:
        try:
            words=shlex.split(input('tab5-storage [{}] {}> '.format('rw' if args.rw else 'ro',args.root)))
            if not words:continue
            op=words[0]
            if op in ('quit','exit'):return 0
            if op=='ls' and len(words)<=2:
                for name in client.names(path(words[1]) if len(words)==2 else args.root):
                    print(json.dumps(name,ensure_ascii=False))
            elif op=='get' and len(words)==3:
                print('received {} bytes'.format(get_file(client,path(words[1]),words[2])))
            elif op in ('put','mkdir','rm'):
                if not args.rw:raise StorageError('write command requires connect --rw and server write permission')
                if op=='put' and (len(words)==3 or len(words)==4 and words[3]=='--overwrite'):
                    with open(words[1],'rb') as source:
                        print('sent {} bytes'.format(client.upload(source,path(words[2]),len(words)==4)))
                elif op=='mkdir' and len(words)==2:client.mkdir(path(words[1]))
                elif op=='rm' and len(words)==2:client.remove(path(words[1]))
                else:print('Invalid arguments; paths with spaces must be quoted.')
            else:print('Commands: ls/get/put/mkdir/rm/quit. get REMOTE LOCAL; put LOCAL REMOTE [--overwrite].')
        except EOFError:return 0
        except (StorageError,OSError,ValueError,argparse.ArgumentTypeError) as exc:
            print('tab5-storage: '+str(exc),file=sys.stderr)

def parser():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command',required=True)
    sub.add_parser('doctor')
    for name in ('connect','mount','ls','get','put','mkdir','rm'):
        s = sub.add_parser(name)
        s.add_argument('host',type=identity)
        s.add_argument('--port',type=port_number,default=22)
        s.add_argument('--user',type=identity)
        if name in ('connect','mount'):
            s.add_argument('root',type=lambda v:remote_path(v,True));s.add_argument('mountpoint');s.add_argument('--rw',action='store_true')
        elif name == 'put':
            s.add_argument('local');s.add_argument('remote',type=remote_path);s.add_argument('--overwrite',action='store_true')
        else:
            s.add_argument('remote',type=remote_path)
            if name == 'get':
                s.add_argument('local')
    return p

def main(argv=None):
    args = parser().parse_args(argv)
    try:
        if args.command == 'doctor':
            return doctor()
        if args.command == 'mount':
            return mount(args)
        if args.command == 'connect':
            if mount_ready():
                status=mount(args)
                if status==0:return 0
                if status in (-2,130):return 130
                print('Mount failed (status {}); switching to file transfer.'.format(status),file=sys.stderr)
            else:
                print('FUSE/SSHFS mount unavailable; switching to file transfer. No installation is performed.')
        if not shutil.which('ssh'):
            raise StorageError('OpenSSH ssh not found; no Python package or automatic installation is used')
        proc = subprocess.Popen(ssh_command(args),stdin=subprocess.PIPE,stdout=subprocess.PIPE)
        try:
            client = SFTP(proc.stdout,proc.stdin)
            if args.command == 'connect':
                transfer_prompt(client,args)
            elif args.command == 'ls':
                for name in client.names(args.remote):
                    print(json.dumps(name,ensure_ascii=False))
            elif args.command == 'get':
                print('received {} bytes'.format(get_file(client,args.remote,args.local)))
            elif args.command == 'put':
                with open(args.local,'rb') as source:
                    print('sent {} bytes'.format(client.upload(source,args.remote,args.overwrite)))
            elif args.command == 'mkdir':
                client.mkdir(args.remote)
            elif args.command == 'rm':
                client.remove(args.remote)
        finally:
            for stream in (proc.stdin,proc.stdout):
                try:
                    stream.close()
                except OSError:
                    pass
            try:
                status = proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.terminate()
                try:
                    proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    proc.kill();proc.wait()
                raise StorageError('SSH did not exit after SFTP close')
        if status:
            raise StorageError('SSH exited with status {}'.format(status))
        return 0
    except (StorageError,OSError) as exc:
        print('tab5-storage: '+str(exc),file=sys.stderr)
        if isinstance(exc,StatusError) and exc.code == 8:
            print('The server does not implement this operation (for example SETSTAT or rename).',file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130

if __name__ == '__main__':
    sys.exit(main())
