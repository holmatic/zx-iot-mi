
import os

os.system("dir")
if __name__ == "__main__":
    os.system('''"C:\Program Files (x86)\Tasm32\Tasm.exe" -80 -b zx-iot-mi/main/asm/loader.asm zx-iot-mi/main/asm/loader.p''')
    f=open("zx-iot-mi/main/asm/loader.p",'rb').read()
    hx="const char loadfile[]={"+",".join(["0x%02x"%v for v in f])+"};"
    print( hx )
    print( len(f) )
    os.system('''"C:\Program Files (x86)\Tasm32\Tasm.exe" -80 -b zx-iot-mi/main/asm/menu1k.asm zx-iot-mi/main/asm/menu1k.p''')
    f=open("zx-iot-mi/main/asm/menu1k.p",'rb').read()
    hx="const char menufile[]={"+",".join(["0x%02x"%v for v in f])+"};"
    print( hx )
    print( len(f) )
