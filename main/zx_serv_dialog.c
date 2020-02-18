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
#include "zx_file_img.h"
#include "signal_to_zx.h"
#include "zx_server.h"
#include "zx_serv_dialog.h"

static const char *TAG = "zx_srv_dlg";





static char txt_buf[33];

#define FILFB_SIZE 32
#define FILENAME_SIZE 24


/* lookup/jump table  for response from ZX */
typedef bool (*resp_func) (const char*, int);

typedef struct {
    uint8_t zxkey;  
    resp_func func;
    char func_arg_s[FILENAME_SIZE];
    int func_arg_i;  
} zxsrv_menu_response_entry;

static zxsrv_menu_response_entry menu_response[20];

static uint8_t menu_resp_size=0;
//static char curr_dir[];

static void clear_mrespond_entries();   
static void create_mrespond_entry(uint8_t zxkey, resp_func func, const char* func_arg_s,  int func_arg_i);


static bool zxsrv_respond_filemenu(const char *dirpath, int); // fwd declare
static bool zxsrv_respond_fileload(const char *filepath, int dummy);
static bool zxsrv_respond_inpstr(const char *question, int offset);





static void clear_mrespond_entries(){
    menu_resp_size=0;
}

static void create_mrespond_entry(uint8_t zxkey, resp_func func, const char* func_arg_s,  int func_arg_i){
    menu_response[menu_resp_size].zxkey=zxkey;
    menu_response[menu_resp_size].func=func;
    strlcpy(menu_response[menu_resp_size].func_arg_s, func_arg_s, FILENAME_SIZE);
    menu_response[menu_resp_size].func_arg_i=func_arg_i;
    menu_resp_size++;
}

void zxdlg_reset(){
    clear_mrespond_entries();
    create_mrespond_entry(0, zxsrv_respond_filemenu, "/spiffs/", 0 );
}

bool zxdlg_respond_from_key(uint8_t zxkey){
    uint8_t e=0;
    for (e=0; e<menu_resp_size; e++){
        if( menu_response[e].zxkey == zxkey || e+1==menu_resp_size ){
            return (*menu_response[e].func)(menu_response[e].func_arg_s, menu_response[e].func_arg_i);
        }
    }
    return false;
}


bool zxdlg_respond_from_string(uint8_t* strg, uint8_t len){
    if (menu_resp_size)
        return (*menu_response[0].func)((char*)strg, len);
    return false;
}


static bool zxsrv_respond_fileload(const char *filepath, int dummy){
    int rdbyte;
    uint16_t fpos=0;
    FILE *fd = NULL;

    ESP_LOGI(TAG, "FILELOAD : %s", filepath);

    clear_mrespond_entries();

    fd = fopen(filepath, "rb");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
        return false;
    }
    while(1) {
        rdbyte=fgetc(fd);
        if (rdbyte==EOF) break;
        zxfimg_set_img(fpos++, rdbyte);
    } 
    /* Close file after read complete */
    fclose(fd);
    // next - main menu
    clear_mrespond_entries();
    return true; // to send_zxf_image_compr();zxfimg_delete();
}


static bool zxsrv_retrieve_wlanpasswd(const char *inp, int len){
    char namebuf[FILFB_SIZE];
    ESP_LOGI(TAG, "zxsrv_retrieve_wlanpasswd:");
 
    zx_string_to_ascii((uint8_t*)inp,len,namebuf);
    ESP_LOGI(TAG, "WLANPASSWD : %s", namebuf);

    return zxsrv_respond_filemenu("/spiffs/", 0);    
}


static bool zxsrv_respond_inpstr(const char *question, int offset){

   // const char *dirpath="/spiffs/";
    zxfimg_create(ZXFI_STR_INP);
    sprintf(txt_buf,"[ INPUT STRING ] ");
    zxfimg_print_video(1,txt_buf);

    zxfimg_print_video(3,question);

    clear_mrespond_entries();
    /* append default entry */
    create_mrespond_entry(0, zxsrv_retrieve_wlanpasswd, "", 0 );
    return true; // to send_zxf_image_compr();zxfimg_delete();
}




static bool zxsrv_respond_filemenu(const char *dirpath, int offset){

   // const char *dirpath="/spiffs/";
    char entrypath[ESP_VFS_PATH_MAX+17];
    char entrysize[16];
    const char *entrytype;
    uint8_t entry_num=0;
    struct dirent *entry;
    struct stat entry_stat;
    DIR *dir = opendir(dirpath);
    const size_t dirpath_len = strlen(dirpath);


    ESP_LOGI(TAG, "FILEMENU : %s", dirpath);
    /* Retrieve the base path of file storage to construct the full path */
    strlcpy(entrypath, dirpath, sizeof(entrypath));

    if (!dir) {
        ESP_LOGE(TAG, "Failed to stat dir : %s", dirpath);
    //    /* Respond with 404 Not Found */
    //    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Directory does not exist");
        return;
    }
    zxfimg_create(ZXFI_MENU_KEY);
    sprintf(txt_buf,"[ FILE MENU ]: (%s) ",dirpath);
    zxfimg_print_video(1,txt_buf);

    clear_mrespond_entries();
    /* Iterate over all files / folders and fetch their names and sizes */
    while ((entry = readdir(dir)) != NULL && entry_num<16) {
        entrytype = (entry->d_type == DT_DIR ? "(DIR)" : "");

        strlcpy(entrypath + dirpath_len, entry->d_name, sizeof(entrypath) - dirpath_len);
        if (stat(entrypath, &entry_stat) == -1) {
            ESP_LOGE(TAG, "Failed to stat %s : %s", entrytype, entry->d_name);
            continue;
        }
        sprintf(entrysize, "%ld", entry_stat.st_size);
        ESP_LOGI(TAG, "Found %s : %s (%s bytes)", entrytype, entry->d_name , entrysize);
        create_mrespond_entry(entry_num+0x1c, zxsrv_respond_fileload,  entrypath, 0 );

        sprintf(txt_buf," [%X] %s %s",entry_num,entry->d_name,entrytype);
        zxfimg_print_video(entry_num+3,txt_buf);
        entry_num++;
    }
    /* append default entry */
    create_mrespond_entry(55, zxsrv_respond_inpstr, "INP-QU", 0 ); // "R"
    create_mrespond_entry(0, zxsrv_respond_filemenu, "/spiffs/", 0 );
    closedir(dir);
    return true; // to send_zxf_image_compr();zxfimg_delete();
}

