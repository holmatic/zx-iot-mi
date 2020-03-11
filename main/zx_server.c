/* ZX Server

Controls communication to the ZX computer by listening to signal_from and 
sending data via signal_to modules.

Works asynchroously, thus communication is done via queues

*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "zx_serv_dialog.h"
#include "zx_server.h"
#include "zx_file_img.h"
#include "signal_to_zx.h"

static const char *TAG = "zx_server";

static QueueHandle_t msg_queue=NULL;


#define FILFB_SIZE 48
#define FILENAME_SIZE 24

static uint8_t file_first_bytes[FILFB_SIZE]; // storage for analyzing the file name or commands etc
static uint8_t file_name_len=0; 



static void send_zxf_loader_uncompressed(){
    uint16_t imgsize;
    ESP_LOGI(TAG,"Send (uncompressed) loader \n");
    zxfimg_create(ZXFI_LOADER);
    imgsize=zxfimg_get_size();
    stzx_send_cmd(STZX_FILE_START,FILE_TAG_NORMAL);
    stzx_send_cmd(STZX_FILE_DATA,0xA6);       /* one byte file name */             
    for(size_t i=0;i<imgsize;i++){
        stzx_send_cmd(STZX_FILE_DATA,zxfimg_get_img(i));                    
    }
    stzx_send_cmd(STZX_FILE_END,0);
    zxfimg_delete();
}


static void send_zxf_image_compr(){
    uint16_t imgsize=zxfimg_get_size();
    stzx_send_cmd(STZX_FILE_START,FILE_TAG_COMPRESSED);
    for(uint16_t i=0;i<imgsize;i++){
        stzx_send_cmd(STZX_FILE_DATA, zxfimg_get_img(i) );                    
    }
   stzx_send_cmd(STZX_FILE_END,0);
}

static void zxsrv_filename_received(){
    char namebuf[FILFB_SIZE];
    zx_string_to_ascii(file_first_bytes,file_name_len,namebuf);
    ESP_LOGI(TAG,"SAVE file, name [%s]\n",namebuf);
}

static void save_received_zxfimg(){
    FILE *fd = NULL;
    uint16_t i;
    const char *dirpath="/spiffs/";
    char entrypath[ESP_VFS_PATH_MAX+17];
    char namebuf[FILFB_SIZE];
    zx_string_to_ascii(file_first_bytes,file_name_len,namebuf);
    if(NULL==strchr(namebuf,'.') ){ /* add extension if none provided */
        strlcpy(namebuf + strlen(namebuf), ".p", FILFB_SIZE - strlen(namebuf));
    }
    ESP_LOGI(TAG,"SAVE - file [%s]\n",namebuf);
    strlcpy(entrypath, dirpath, sizeof(entrypath));
    strlcpy(entrypath + strlen(dirpath), namebuf, sizeof(entrypath) - strlen(dirpath));
    fd = fopen(entrypath, "wb");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to write to file : %s", entrypath);
        return;
    }
    for(i=0; i<zxfimg_get_size(); i++) {
        fputc( zxfimg_get_img(i), fd );
    } 
    /* Close file after write complete */
    fclose(fd);
    ESP_LOGI(TAG,"SAVE - done\n");
}

static void zxsrv_task(void *arg)
{
    zxserv_event_t evt;
    ESP_LOGI(TAG,"sfzx_task START \n");
    while(true){
		if(pdTRUE ==  xQueueReceive( msg_queue, &evt, 10 / portTICK_RATE_MS ) ) {
			//ESP_LOGI(TAG,"Retrieved evt %d",evt.evt_type);
            if(evt.evt_type==ZXSG_HIGH){
                // Load
                send_zxf_loader_uncompressed();
                // next - main menu
                zxdlg_reset();
            }else if(evt.evt_type==ZXSG_FILE_DATA){
                if(evt.addr<FILFB_SIZE){
                    file_first_bytes[evt.addr]=(uint8_t) evt.data;
                    // extract file name
                    if(evt.addr==0) file_name_len=0;
                    if(!file_name_len){
                        if( evt.data&0x80 ){
                            file_name_len=evt.addr+1;
                            file_first_bytes[evt.addr] ^= 0x80; // convert all name to upper case                       
                            zxsrv_filename_received();
                        } 
                    }
                    if(file_first_bytes[0]==ZX_SAVE_TAG_MENU_RESPONSE+1 && evt.data==0x80){
                            // send compressed second stage
                            ESP_LOGI(TAG,"STRING INPUT addr %d \n",evt.addr); 
                            for(int i=0;i<=evt.addr;i++){
                                ESP_LOGI(TAG,"  STR field %d %02X \n",evt.addr,file_first_bytes[i] ); 
                            }
                            if (zxdlg_respond_from_string(  &file_first_bytes[4], file_first_bytes[2])){
                                send_zxf_image_compr();
                                zxfimg_delete();
                            }
                    }

                    //
                    if(evt.addr==1){
                        if(file_first_bytes[0]==ZX_SAVE_TAG_LOADER_RESPONSE){
                            // send compressed second stage
                            ESP_LOGI(TAG,"Response from %dk ZX, send 2nd (compressed) stage \n",(evt.data-0x40)/4 );                        
                            if (zxdlg_respond_from_key(0)){
                                send_zxf_image_compr();
                                zxfimg_delete();
                            }
                        } else if(file_first_bytes[0]==ZX_SAVE_TAG_MENU_RESPONSE){
                            // send compressed second stage
                            ESP_LOGI(TAG,"MENU RESPONSE KEYPRESS code %02X \n",evt.data); 
                            if (zxdlg_respond_from_key(evt.data)){
                                send_zxf_image_compr();
                                zxfimg_delete();
                            }
                        }
                    }
                }
                if(file_name_len){
                    zxfimg_set_img(evt.addr-file_name_len,evt.data);
                    if(evt.addr>file_name_len+30 && zxfimg_get_size()==1+evt.addr-file_name_len ){
                        save_received_zxfimg();
                        file_name_len=0;
                        zxdlg_reset();
                    }
                }
			}
			else ESP_LOGW(TAG,"Unexpected evt %d",evt.evt_type);
		}
    }
}



void zxsrv_init()
{
    msg_queue=xQueueCreate(50,sizeof( zxserv_event_t ) );
    xTaskCreate(zxsrv_task, "zxsrv_task", 1024 * 3, NULL, 8, NULL);
}


void zxsrv_send_msg_to_srv( zxserv_evt_type_t msg, uint16_t addr, uint16_t data)
{
    zxserv_event_t evt;
	evt.evt_type=msg;
	evt.addr=addr;
	evt.data=data;
	if( xQueueSendToBack( msg_queue,  &evt, 100 / portTICK_RATE_MS ) != pdPASS )
	{
		// Failed to post the message, even after 100 ms.
		ESP_LOGE(TAG, "Server write queue blocked");
	}
}
