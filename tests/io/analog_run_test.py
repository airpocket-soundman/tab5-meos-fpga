from pathlib import Path
import argparse,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-analog-') as d:
 exe=Path(d)/'analog.exe'
 subprocess.run([a.cc,'cc','-O1','-UNDEBUG','-I',str(r/'kernel/include'),str(r/'tests/io/analog_test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)

with tempfile.TemporaryDirectory(prefix='meos-adc-native-') as d:
 source=(r/'hal/esp32p4/io_analog.c').read_text(encoding='utf-8')
 start=source.rindex('static int adc_native_read(unsigned unit')
 end=source.index('\n#endif',start)
 model=(r/'tests/io/analog_adc_native_test.c').read_text(encoding='utf-8')
 generated=Path(d)/'native.c';generated.write_text(model.replace('/* NATIVE_ADC */',source[start:end]),encoding='utf-8')
 exe=Path(d)/'native.exe'
 subprocess.run([a.cc,'cc','-O1','-UNDEBUG','-include','stdbool.h','-I',str(r/'kernel/include'),str(generated),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
