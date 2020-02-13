/* ZX Server

Controls communication to the ZX computer by listening to signal_from and 
sending data via signal_to modules.

Works asynchroously, thus communication is done via queues

*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/unistd.h>
#include "esp_err.h"
#include "esp_log.h"


#include "zx_file_img.h"

static const char *TAG = "zx_file_img";





//static const uint8_t menufile[]={0x00,0x00,0x00,0x02,0x41,0x03,0x41,0x22,0x41,0x00,0x00,0x23,0x41,0x22,0x41,0x00,0x00,0x23,0x41,0x23,0x41,0x00,0x5d,0x40,0x00,0x02,0x01,0x00,0xff,0xff,0xff,0x37,0xc2,0x40,0x00,0x00,0x00,0x00,0x00,0x8d,0x0c,0x00,0x00,0xff,0xff,0x00,0x00,0xbc,0x21,0x18,0x40,0x0e,0xfe,0x06,0x08,0xdb,0xfe,0x17,0x30,0xfb,0x10,0xfe,0x06,0x08,0xed,0x50,0xcb,0x12,0x17,0x16,0x04,0x15,0x20,0xfd,0x10,0xf4,0x77,0xcd,0xfc,0x01,0x18,0xe1,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x41,0x00,0xea,0xcd,0xe7,0x02,0xcd,0x46,0x0f,0x06,0xa0,0xc5,0x06,0x00,0x10,0xfe,0xc1,0x10,0xf8,0x1e,0x49,0xcd,0x1f,0x03,0x1e,0x01,0xcd,0x1f,0x03,0x1e,0x00,0xcd,0x1f,0x03,0xe1,0xe1,0xe1,0xe1,0xe1,0xe1,0xe1,0x21,0x76,0x06,0xe3,0x21,0x3c,0x40,0x11,0xba,0x7f,0x01,0x20,0x00,0xed,0xb0,0x21,0x15,0x40,0x34,0x21,0x09,0x40,0xc3,0xba,0x7f,0x76,0x00,0x0a,0x06,0x00,0xf1,0x26,0x0d,0x14,0x41,0x76,0x00,0x14,0x11,0x00,0xfa,0x26,0x0d,0x14,0x0b,0x0b,0xde,0xec,0x1d,0x1c,0x7e,0x84,0x20,0x00,0x00,0x00,0x76,0x00,0x1e,0x0e,0x00,0xf4,0xc5,0x0b,0x1d,0x22,0x21,0x1f,0x22,0x0b,0x1a,0xc4,0x26,0x0d,0x76,0x00,0x32,0x0b,0x00,0xf5,0xd4,0xc5,0x0b,0x1d,0x22,0x21,0x1d,0x20,0x0b,0x76,0x76,0x3f,0x3d,0x00,0x00,0x2e,0x34,0x39,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x80};
                              //{0x00,0x00,0x00,0xf7,0x40,0xf8,0x40,0x17,0x41,0x00,0x00,0x18,0x41,0x17,0x41,0x00,0x00,0x18,0x41,0x18,0x41,0x00,0x5d,0x40,0x00,0x02,0x01,0x00,0xff,0xff,0xff,0x37,0xb7,0x40,0x00,0x00,0x00,0x00,0x00,0x8d,0x0c,0x00,0x00,0xff,0xff,0x00,0x00,0xbc,0x21,0x18,0x40,0x0e,0xfe,0x06,0x08,0xdb,0xfe,0x17,0x30,0xfb,0x10,0xfe,0x06,0x08,0xed,0x50,0xcb,0x12,0x17,0x16,0x04,0x15,0x20,0xfd,0x10,0xf4,0x77,0xcd,0xfc,0x01,0x18,0xe1,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x36,0x00,0xea,0xcd,0xe7,0x02,0xcd,0x46,0x0f,0x06,0xa0,0xc5,0x06,0x00,0x10,0xfe,0xc1,0x10,0xf8,0x1e,0x49,0xcd,0x1f,0x03,0x1e,0x01,0xcd,0x1f,0x03,0x1e,0x00,0xcd,0x1f,0x03,0xe1,0xe1,0xe1,0xe1,0xe1,0xe1,0xe1,0x21,0x76,0x06,0xe3,0x21,0x15,0x40,0x34,0x21,0x09,0x40,0xc3,0x3c,0x40,0x76,0x00,0x0a,0x06,0x00,0xf1,0x26,0x0d,0x14,0x41,0x76,0x00,0x14,0x11,0x00,0xfa,0x26,0x0d,0x14,0x0b,0x0b,0xde,0xec,0x1d,0x1c,0x7e,0x84,0x20,0x00,0x00,0x00,0x76,0x00,0x1e,0x0e,0x00,0xf4,0xc5,0x0b,0x1d,0x22,0x21,0x1f,0x22,0x0b,0x1a,0xc4,0x26,0x0d,0x76,0x00,0x32,0x0b,0x00,0xf5,0xd4,0xc5,0x0b,0x1d,0x22,0x21,0x1d,0x20,0x0b,0x76,0x76,0x3f,0x3d,0x00,0x00,0x2e,0x34,0x39,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x80};

//static const uint8_t ldrfile[]=  {       0x00,0x00,0x00,0xc0,0x40,0xc1,0x40,0xe0,0x40,0x00,0x00,0xe1,0x40,0xe0,0x40,0x00,0x00,0xe1,0x40,0xe1,0x40,0x00,0x5d,0x40,0x00,0x02,0x01,0x00,0xff,0xff,0xff,0x37,0xb1,0x40,0x00,0x00,0x00,0x00,0x00,0x8d,0x0c,0x00,0x00,0xff,0xff,0x00,0x00,0xbc,0x21,0x18,0x40,0x0e,0xfe,0x06,0x08,0xdb,0xfe,0x17,0x30,0xfb,0x10,0xfe,0x06,0x08,0xed,0x50,0xcb,0x12,0x17,0x16,0x04,0x15,0x20,0xfd,0x10,0xf4,0x77,0xcd,0xfc,0x01,0x18,0xe1,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x30,0x00,0xea,0xcd,0xe7,0x02,0xcd,0x46,0x0f,0x06,0xc8,0xc5,0x06,0x00,0x10,0xfe,0xc1,0x10,0xf8,0x1e,0x46,0xcd,0x1f,0x03,0x21,0x05,0x40,0xcd,0x1e,0x03,0x21,0x76,0x06,0x1e,0x00,0xcd,0x1f,0x03,0xe3,0x21,0x15,0x40,0x34,0x21,0x09,0x40,0xc3,0x3c,0x40,0x76,0x00,0x0a,0x0b,0x00,0xf5,0xd4,0xc5,0x0b,0x1d,0x22,0x21,0x1d,0x20,0x0b,0x76,0x76,0x3f,0x3d,0x00,0x2e,0x34,0x00,0x39,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x80};

//static const uint8_t ldrfile[]= {0x00,0x00,0x00,0xc3,0x40,0xc4,0x40,0xe3,0x40,0x00,0x00,0xe4,0x40,0xe3,0x40,0x00,0x00,0xe4,0x40,0xe4,0x40,0x00,0x5d,0x40,0x00,0x02,0x01,0x00,0xff,0xff,0xff,0x37,0xb4,0x40,0x00,0x00,0x00,0x00,0x00,0x8d,0x0c,0x00,0x00,0xff,0xff,0x00,0x00,0xbc,0x21,0x18,0x40,0x0e,0xfe,0x06,0x08,0xdb,0xfe,0x17,0x30,0xfb,0x10,0xfe,0x06,0x08,0xed,0x50,0xcb,0x12,0x17,0x16,0x04,0x15,0x20,0xfd,0x10,0xf4,0x77,0xcd,0xfc,0x01,0x18,0xe1,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x33,0x00,0xea,0xcd,0xe7,0x02,0xcd,0x46,0x0f,0x06,0xc8,0xc5,0x06,0x00,0x10,0xfe,0xc1,0x10,0xf8,0x1e,0x46,0xcd,0x1f,0x03,0x21,0x05,0x40,0xcd,0x1e,0x03,0x1e,0x00,0xcd,0x1f,0x03,0xe1,0xe1,0xe1,0x21,0x76,0x06,0xe3,0x21,0x15,0x40,0x34,0x21,0x09,0x40,0xc3,0x3c,0x40,0x76,0x00,0x0a,0x0b,0x00,0xf5,0xd4,0xc5,0x0b,0x1d,0x22,0x21,0x1d,0x20,0x0b,0x76,0x76,0x3f,0x3d,0x00,0x2e,0x34,0x00,0x39,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x80};

#include "asm_code.c"

static const uint8_t* base_img[ZXFI_NUM]={ldrfile,menufile,NULL};



/* Helper for text code conversion */

static const char* CODETABLE="#_~\"@$:?()><=+-*/;,.0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";	/* ZX81 code table, starting at 28 */
static uint8_t convert_ascii_to_zx_code(int ascii_char)
{
	uint8_t zx_code=0;
	int upper_ascii_c=toupper(ascii_char);

    if(ascii_char==' ') return 0;
	while(CODETABLE[zx_code])
	{
		if(CODETABLE[zx_code]==upper_ascii_c) return 8+zx_code;		/* Exit with result on match */
		zx_code++;
	}
	/* return a default */
	return 4;
}


static uint8_t zx_txt_buf[33];

static uint16_t convert_ascii_to_zx_str(const char* ascii_str) // return length
{
	uint8_t* d=zx_txt_buf;
	uint8_t inverse=0;
	uint16_t len=0;
	char c;
	while(  (c=*ascii_str++)  ){
		if(c=='[') inverse=0x80;
		else if(c==']') inverse=0;
		else{
			*d++ = convert_ascii_to_zx_code(c) | inverse;
			len++;
		}
	}
	return len;
}


static uint8_t *memimg=0;


static const uint16_t img_offs=16393;




static uint16_t mem_rd16(uint16_t memaddr)
{
	return memimg[memaddr-img_offs]+256*memimg[memaddr+1-img_offs];
}

static void  mem_wr16(uint16_t memaddr, uint16_t data)
{
	memimg[memaddr-img_offs]=data&0x00ff;
	memimg[memaddr+1-img_offs]= (data&0xff00)>>8;
}

static uint16_t mem_img_size(){
	return memimg[16404-img_offs]+256*memimg[16404+1-img_offs] - img_offs;
}

static void mem_insert(uint8_t* src, uint16_t insert_pos, uint16_t ins_size){
	uint16_t v,p,old_sz;
	old_sz=mem_img_size();
	// update pointers
	for(v=16396;v<=16412;v+=2)	{
		p=mem_rd16(v);
		if (p>=insert_pos){
			mem_wr16(v,p+ins_size);
		}
	}
	// move
	for(v=old_sz-1; v>=insert_pos-img_offs;v--)	{
		memimg[v+ins_size]=memimg[v];
	}
	// fill
	for(v=0; v<ins_size;v++){
		memimg[v+insert_pos-img_offs]=src[v];
	}
}

void zxfimg_print_video(uint8_t linenum, const char* asciitxt) {
	uint16_t dfile_pos=mem_rd16(16396);//dfile
	while(linenum){
		if(memimg[dfile_pos++ -img_offs]==0x76) linenum--;
	}
	mem_insert(zx_txt_buf,dfile_pos,convert_ascii_to_zx_str(asciitxt));
}


// call once at startup
void zxfimg_create(zxfimg_prog_t prog_type) {
	uint16_t i,sz;
    const uint8_t *src= base_img[prog_type];
	if(!memimg) memimg=calloc(16384,1);
	for(i=0;i<16507-img_offs;i++){
		memimg[i]=src[i];
	}
	sz=mem_img_size();
	for(;i<sz;i++){
		memimg[i]=src[i];
	}
}

uint8_t zxfimg_get_img(uint16_t filepos) {
    //if(filepos>=16444-img_offs && filepos<16477-img_offs){
	//if(filepos>=16477-img_offs && filepos<16508-img_offs){		
	//	if(memimg[filepos]!=0 && memimg[filepos]!=ldrfile[filepos]) ESP_LOGW(TAG, "Printer buf %d contains %02x",filepos+img_offs,memimg[filepos]);
	//	return ldrfile[filepos-33]; 
	//}
    return memimg[filepos];
}

void  zxfimg_set_img(uint16_t filepos,uint8_t data) {
	if(!memimg) memimg=calloc(16384,1);
	memimg[filepos]=data;
}




uint16_t zxfimg_get_size() {
    return mem_img_size();
}

void zxfimg_delete() {
	if(memimg) free(memimg);
	memimg=NULL;
}