
import os

os.system("dir")
if __name__ == "__main__":
    os.system('''"C:\Program Files (x86)\Tasm32\Tasm.exe" -80 -b zx-iot-mi/main/asm/loader.asm zx-iot-mi/main/asm/loader.p''')
    f=open("zx-iot-mi/main/asm/loader.p",'rb').read()
    hx="const char pfile[]={"+",".join(["0x%02x"%v for v in f])+"};"
    print( hx )
    print( len(f) )