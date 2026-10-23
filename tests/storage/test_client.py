import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('tab5_storage',ROOT/'tools/tab5_storage.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

def packet(kind,data): return m.u32(1+len(data))+bytes([kind])+data

def fake_ssh():
    source=sys.stdin.buffer;out=sys.stdout.buffer
    files={'/sd/hello.txt':b'hello\x00world'};handles={};next_h=1
    def exact(n):
        data=source.read(n)
        if len(data)!=n: raise EOFError
        return data
    def send(kind,data):
        data=packet(kind,data)
        for i in range(0,len(data),3):out.write(data[i:i+3]);out.flush()
    def status(ident,code=0,message=''):
        send(101,m.u32(ident)+m.u32(code)+m.string(message)+m.string(''))
    while True:
        head=source.read(4)
        if not head:break
        n=struct.unpack('>I',head)[0];raw=exact(n);kind=raw[0];r=m.Reader(raw[1:])
        if kind==1:
            assert r.uint()==3;send(2,m.u32(3));continue
        ident=r.uint()
        if kind in (3,11):
            path=r.string().decode()
            if kind==3:
                flags=r.uint();r.attrs();r.end()
                if flags&32 and path in files:status(ident,4,'file exists');continue
                if flags==1 and path not in files:status(ident,2,'missing');continue
                if flags&16:status(ident,8,'truncate unavailable');continue
                if flags&8:files[path]=b''
            h=str(next_h).encode();next_h+=1;handles[h]=(path,False)
            send(102,m.u32(ident)+m.string(h))
        elif kind==4:
            del handles[r.string()];status(ident)
        elif kind==5:
            path,_=handles[r.string()];offset=struct.unpack('>Q',r.take(8))[0];length=r.uint()
            chunk=files[path][offset:offset+min(length,4)]
            if chunk:send(103,m.u32(ident)+m.string(chunk))
            else:status(ident,1)
        elif kind==6:
            path,_=handles[r.string()];offset=struct.unpack('>Q',r.take(8))[0];data=r.string()
            assert offset==len(files[path]);files[path]+=data;status(ident)
        elif kind==12:
            h=r.string();path,done=handles[h]
            if done:status(ident,1);continue
            handles[h]=(path,True)
            names=[p.rsplit('/',1)[1] for p in files if p.startswith(path+'/')]
            body=m.u32(ident)+m.u32(len(names))
            for name in names:body+=m.string(name)+m.string(name)+m.u32(0)
            send(104,body)
        elif kind==13:
            files.pop(r.string().decode(),None);status(ident)
        elif kind==14:
            r.string();r.attrs();status(ident)
        else:status(ident,8)

class Fragmented(io.BytesIO):
    def read(self,n=-1):return super().read(min(n,2) if n>=0 else 2)

class Tests(unittest.TestCase):
    def connection(self):
        proc=subprocess.Popen([sys.executable,'-u',__file__,'--mock-ssh'],stdin=subprocess.PIPE,stdout=subprocess.PIPE)
        self.addCleanup(lambda:proc.wait(timeout=3))
        self.addCleanup(proc.stdout.close);self.addCleanup(proc.stdin.close)
        return m.SFTP(proc.stdout,proc.stdin)
    def test_real_pipes_fragmented_roundtrip(self):
        c=self.connection();self.assertEqual(list(c.names('/sd')),['hello.txt'])
        data=io.BytesIO();self.assertEqual(c.download('/sd/hello.txt',data),11)
        self.assertEqual(data.getvalue(),b'hello\x00world')
        content=bytes(range(256))*200
        self.assertEqual(c.upload(io.BytesIO(content),'/usb/new.bin'),len(content))
        out=io.BytesIO();c.download('/usb/new.bin',out);self.assertEqual(out.getvalue(),content)
        with self.assertRaises(m.StatusError):c.upload(io.BytesIO(b'x'),'/sd/hello.txt')
        with self.assertRaises(m.StatusError) as e:c.upload(io.BytesIO(b'x'),'/sd/hello.txt',True)
        self.assertEqual(e.exception.code,8)
        c.mkdir('/sd/new');c.remove('/usb/new.bin')
    def test_wire_validation(self):
        c=m.SFTP(Fragmented(packet(2,m.u32(3))),io.BytesIO())
        self.assertEqual(c.sequence,0)
        for wire in (b'\x00\x00',m.u32(0),m.u32(m.MAX_PACKET+1),packet(2,m.u32(4)),packet(101,m.u32(0))):
            with self.assertRaises(m.StorageError):m.SFTP(Fragmented(wire),io.BytesIO())
        wire=packet(2,m.u32(3))+packet(101,m.u32(99)+m.u32(0)+m.string('')+m.string(''))
        c=m.SFTP(Fragmented(wire),io.BytesIO())
        with self.assertRaises(m.StorageError):c.mkdir('/sd/x')
    def test_roots_and_identity(self):
        for path in ('/sd','/sd/a b','/usb/a','/usb1/x','/usb3/x'):
            self.assertEqual(m.remote_path(path),path)
        for path in ('/','/etc/passwd','/usb4/a','/sd/../usb/a','relative','/sd/\x00bad'):
            with self.assertRaises(Exception):m.remote_path(path)
        for value in ('-oProxyCommand=x','host;touch x','host\n-ofoo',''):
            with self.assertRaises(Exception):m.identity(value)
        args=m.parser().parse_args(['get','tab5','/sd/a b','local','--port','2222','--user','meos'])
        cmd=m.ssh_command(args)
        self.assertEqual(cmd,['ssh','-T','-o','ConnectTimeout=10','-p','2222','-l','meos','-s','tab5','sftp'])
        self.assertNotIn('/sd/a b',cmd)
    def test_local_exclusive_and_cleanup(self):
        class Client:
            def download(self,remote,f):f.write(b'new');return 3
        class Broken:
            def download(self,remote,f):f.write(b'partial');raise m.StorageError('network failed')
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'file'
            self.assertEqual(m.get_file(Client(),'/sd/x',path),3)
            with self.assertRaises(FileExistsError):m.get_file(Client(),'/sd/x',path)
            self.assertEqual(path.read_bytes(),b'new')
            with self.assertRaises(m.StorageError):m.get_file(Broken(),'/sd/x',Path(d)/'failed')
            self.assertEqual([p.name for p in Path(d).iterdir()],['file'])
    def test_main_uses_ssh_without_shell(self):
        real=subprocess.Popen;seen=[]
        def spawn(command,**kwargs):
            seen.append(command);self.assertNotIn('shell',kwargs)
            return real([sys.executable,'-u',__file__,'--mock-ssh'],**kwargs)
        with mock.patch.object(m.shutil,'which',return_value='/usr/bin/ssh'),mock.patch.object(m.subprocess,'Popen',side_effect=spawn),mock.patch('sys.stdout',new_callable=io.StringIO) as output:
            self.assertEqual(m.main(['ls','tab5','/sd']),0)
            self.assertIn('hello.txt',output.getvalue())
        self.assertEqual(seen[0][-3:],['-s','tab5','sftp'])
    def test_connect_fallback_prompt(self):
        real=subprocess.Popen
        def spawn(command,**kwargs):return real([sys.executable,'-u',__file__,'--mock-ssh'],**kwargs)
        with tempfile.TemporaryDirectory() as d, mock.patch.object(m,'mount_ready',return_value=False), mock.patch.object(m.shutil,'which',return_value='/usr/bin/ssh'),mock.patch.object(m.subprocess,'Popen',side_effect=spawn),mock.patch('builtins.input',side_effect=['ls','put missing.bin new.bin','ls /usb','quit']),mock.patch('sys.stdout',new_callable=io.StringIO) as output,mock.patch('sys.stderr',new_callable=io.StringIO) as errors:
            self.assertEqual(m.main(['connect','tab5','/sd',d]),0)
            self.assertIn('hello.txt',output.getvalue())
            self.assertIn('write command requires',errors.getvalue())
            self.assertIn('outside the selected root',errors.getvalue())
    def test_connect_uses_mount_when_ready(self):
        with mock.patch.object(m,'mount_ready',return_value=True),mock.patch.object(m,'mount',return_value=0) as call:
            self.assertEqual(m.main(['connect','tab5','/sd','/tmp/tab5']),0)
            self.assertFalse(call.call_args.args[0].rw)
    def test_mount_readonly_foreground(self):
        with tempfile.TemporaryDirectory() as d, mock.patch.object(m.shutil,'which',return_value='/usr/bin/sshfs'),mock.patch.object(m.os,'access',return_value=True),mock.patch.object(m.subprocess,'call',return_value=0) as call:
            args=m.parser().parse_args(['mount','tab5','/sd',d])
            self.assertEqual(m.mount(args),0)
            command=call.call_args.args[0]
            self.assertIn('-f',command);self.assertIn('ro',command);self.assertNotIn('rw',command)
            args.rw=True;m.mount(args);self.assertIn('rw',call.call_args.args[0])
        with mock.patch.object(m.shutil,'which',return_value=None):
            with self.assertRaises(m.StorageError):m.mount(args)

if __name__=='__main__':
    if sys.argv[1:]==['--mock-ssh']:fake_ssh()
    else:unittest.main()
