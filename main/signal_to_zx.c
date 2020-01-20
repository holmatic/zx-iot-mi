#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_spi_flash.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2s.h"
#include "signal_to_zx.h"


static const char* TAG = "stzx";


//i2s number
#define STZX_I2S_NUM           (1)
//i2s sample rate
// need at least two sample points in one 256/6.5=39.4 us line, so less than 19us, for expmple 16.67us 
#define STZX_I2S_SAMPLE_RATE   (4000) // times 32 is 8us/bit = 64us/byte
#define USEC_TO_BYTE_SAMPLES(us)   (us*STZX_I2S_SAMPLE_RATE*4/1000000) 
#define MILLISEC_TO_BYTE_SAMPLES(ms)   (ms*STZX_I2S_SAMPLE_RATE*4/1000) 



//i2s data bits
// Will send out 15bits MSB first
#define STZX_I2S_SAMPLE_BITS   (16)

//I2S read buffer length in bytes
// we want to buffer several millisecs to be able to reschedule freely; on the
// other side, we do not want too many delays in order to be reactive, so say 10ms
#define STZX_I2S_WRITE_LEN_SAMPLES      (STZX_I2S_SAMPLE_RATE/25)
#define STZX_I2S_WRITE_LEN_BYTES      (STZX_I2S_WRITE_LEN_SAMPLES * STZX_I2S_SAMPLE_BITS/8)



static void stzx_task(void*arg);

static QueueHandle_t event_queue=NULL;
static uint8_t* i2s_writ_buff=NULL;
static  QueueHandle_t file_data_queue=NULL;



/**
 * @brief I2S ADC/DAC mode init.
 */
void stzx_init()
{
	int i2s_num = STZX_I2S_NUM;
	i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate =  STZX_I2S_SAMPLE_RATE,
        .bits_per_sample = STZX_I2S_SAMPLE_BITS,
	    .communication_format =  I2S_COMM_FORMAT_I2S_MSB,
	    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
	    .intr_alloc_flags = 0,
	    .dma_buf_count = 4,
	    .dma_buf_len = STZX_I2S_WRITE_LEN_SAMPLES,
	    .use_apll = 0,//1, True cause problems at high rates (?)
	};
	 //install and start i2s driver
    //xQueueCreate(5, sizeof(i2s_event_t));
	ESP_ERROR_CHECK( i2s_driver_install(i2s_num, &i2s_config, 5, &event_queue ));
	//init DOUT pad
    static const i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_PIN_NO_CHANGE,
        .ws_io_num = I2S_PIN_NO_CHANGE,
        .data_out_num = 22,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    i2s_set_pin(i2s_num, &pin_config);
    
    i2s_writ_buff=(uint8_t*) calloc(STZX_I2S_WRITE_LEN_BYTES, sizeof(uint8_t));
    if(!i2s_writ_buff) printf("calloc of %d failed\n",STZX_I2S_WRITE_LEN_BYTES);


    xTaskCreate(stzx_task, "stzx_task", 1024 * 2, NULL, 8, NULL);
}


static inline void set_sample(uint8_t* samplebuf, uint32_t ix, uint8_t val)
{
    samplebuf[ix^0x0003]=val;//0xff;// val; // convert for endian
}


// demo p file
const uint8_t pfile[]={  0xA6,  0x0,0xa,0x0,0x88,0x40,0x89,0x40,0xa1,0x40,0x0,0x0,0xa2,0x40,0xaa,0x40,0x0,0x0,0xab,0x40,0xab,0x40,0x0,0x5d,0x40,0x0,0x2,0x0,0x0,0xbf,0xfd,0xff,0x37,0x88,0x40,0x0,0x0,0x0,0x0,0x0,0x8d,0xc,0x0,0x0,0xa2,0xf8,0x0,0x0,0xbc,0x21,0x18,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x76,0x0,0x0,0x0,0x0,0x0,0x0,0x84,0x0,0x0,0x0,0x84,0xa0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xa,0x7,0x0,0xea,0x2d,0x2a,0x31,0x31,0x34,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x76,0x80};

const uint8_t wav_zero[]={  0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00};
const uint8_t wav_one[]={  0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00, 0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00,  0x00,0xff,0xff,0x00 };

typedef struct zxfile_wr_status_info
{
    const uint8_t* wavsample; // pointer to sample, if zero, play
    uint32_t remaining_wavsamples; //
    uint32_t bitcount;
    uint8_t data;
} zxfile_wr_status_t;

static zxfile_wr_status_t zxfile;    // some signal statistics

// return true if end of file reached
static bool fill_buf_from_file(uint8_t* samplebuf, QueueHandle_t dataq, size_t buffered_filesize)
{
	bool end_f=false;
    uint32_t ix=0;
    while(ix<STZX_I2S_WRITE_LEN_BYTES) {
        if(zxfile.remaining_wavsamples){
            set_sample(samplebuf,ix++, zxfile.wavsample ? *zxfile.wavsample++ : 0 );
            --zxfile.remaining_wavsamples;
        } else {
            if (zxfile.wavsample){ // after sample, always insert silence
                zxfile.wavsample=NULL;
                zxfile.bitcount++;  // prepare for next bit
                zxfile.remaining_wavsamples=USEC_TO_BYTE_SAMPLES(1300);
            } else {
                if(zxfile.bitcount < buffered_filesize*8 ) {
                    if( (zxfile.bitcount&7) ==0){
                    	//zxfile.data=pfile[zxfile.bitcount>>3];
                		if(pdTRUE != xQueueReceive( dataq, &zxfile.data, 0 ) ) ESP_LOGE(TAG, "End of data");
                		//printf("Data 0x%x\n",zxfile.data);
                    }
                    if(zxfile.data & (0x80 >> (zxfile.bitcount&7)  )){
                        zxfile.wavsample=wav_one;
                        zxfile.remaining_wavsamples=sizeof(wav_one);
                    } else {
                        zxfile.wavsample=wav_zero;
                        zxfile.remaining_wavsamples=sizeof(wav_zero);
                    }
                } else {
                	end_f=true;
                    set_sample(samplebuf,ix++,  0 );
                    zxfile.remaining_wavsamples=MILLISEC_TO_BYTE_SAMPLES(500);
                    zxfile.bitcount=0; // repeat file
                }
            }
        }
    }
    return end_f;
}


void stzx_send_cmd(stzx_mode_t cmd, uint8_t data)
{
    i2s_event_t evt;
    static bool file_active=false;
	static size_t fsize;

	if (cmd==STZX_FILE_START){
		if(file_active)
			ESP_LOGE(TAG, "File double-open");
		file_active=true;
		fsize=0;
	}
	else if (cmd==STZX_FILE_DATA){
		if(!file_active)
			ESP_LOGE(TAG, "File not open on data write");
		if(!file_data_queue){
			file_data_queue=xQueueCreate(16384,sizeof(uint8_t));
		}
	    if( xQueueSendToBack( file_data_queue,  &data, 10 / portTICK_RATE_MS ) != pdPASS )
	    {
	        // Failed to post the message, even after 10 ticks.
			ESP_LOGE(TAG, "File write queue blocked");
	    }
	    ++fsize;
    	evt.size=fsize;
	    if(fsize==100){
	    	/* enough bytes in to start off */
	    	evt.type=STZX_FILE_START;
	        if( xQueueSendToBack( event_queue, &evt, 10 / portTICK_RATE_MS ) != pdPASS )	 ESP_LOGE(TAG, "File write event d queue blocked");
	    }
	    else if( fsize%1000==500){
	    	/* provide an update on the buffer level */
	    	evt.type=STZX_FILE_DATA;
	        if( xQueueSendToBack( event_queue, &evt, 10 / portTICK_RATE_MS ) != pdPASS )	 ESP_LOGE(TAG, "File write event d queue blocked");
	    }

	}
	else if (cmd==STZX_FILE_END){
		if(!file_active)
			ESP_LOGE(TAG, "File not open on data write");

		evt.size=fsize;
		if(fsize<100){
			evt.type=STZX_FILE_START;
		    if( xQueueSendToBack( event_queue, &evt, 10 / portTICK_RATE_MS ) != pdPASS )	 ESP_LOGE(TAG, "File write event e queue blocked");
		}
		evt.type=STZX_FILE_END;
		if( xQueueSendToBack( event_queue, &evt, 10 / portTICK_RATE_MS ) != pdPASS )	 ESP_LOGE(TAG, "File write event queue blocked");
		file_active=false;
	}

}


static void stzx_task(void*arg)
{
    i2s_event_t evt;
    size_t bytes_written;
    size_t buffered_file_count=0;
    while(1){
		if(pdTRUE ==  xQueueReceive( event_queue, &evt, 10 / portTICK_RATE_MS ) ) { // todo: why need wait time here?
			bytes_written=0;

			if(evt.type==(i2s_event_type_t)STZX_FILE_START){
				buffered_file_count=evt.size;
				ESP_LOGW(TAG, "STZX_FILE_START, %d",buffered_file_count);
			}
			else if(evt.type==(i2s_event_type_t)STZX_FILE_DATA){
				buffered_file_count=evt.size;
				ESP_LOGW(TAG, "STZX_FILE_DATA, %d",buffered_file_count);
			}
			else if(evt.type==(i2s_event_type_t)STZX_FILE_END){
				buffered_file_count=evt.size;
				ESP_LOGW(TAG, "STZX_FILE_END, %d",buffered_file_count);
			}
			else if(evt.type==I2S_EVENT_TX_DONE){
				if (fill_buf_from_file(i2s_writ_buff,file_data_queue,buffered_file_count )) buffered_file_count=0;
				i2s_write(STZX_I2S_NUM, i2s_writ_buff, STZX_I2S_WRITE_LEN_BYTES, &bytes_written, 0);
				if (bytes_written!=STZX_I2S_WRITE_LEN_BYTES){
					ESP_LOGW(TAG, "len mismatch a, %d %d",bytes_written,STZX_I2S_WRITE_LEN_BYTES);
				}
				if (fill_buf_from_file(i2s_writ_buff,file_data_queue,buffered_file_count)) buffered_file_count=0;
				i2s_write(STZX_I2S_NUM, i2s_writ_buff, STZX_I2S_WRITE_LEN_BYTES, &bytes_written, 0);
				if (bytes_written!=STZX_I2S_WRITE_LEN_BYTES){
					ESP_LOGW(TAG, "len mismatch b, %d %d",bytes_written,STZX_I2S_WRITE_LEN_BYTES);
				}
			}else{
				ESP_LOGW(TAG, "Unexpected evt %d",evt.type);
			}
		}
    }
}

