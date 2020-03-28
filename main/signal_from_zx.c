#include <stdio.h>
#include <string.h>
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_spi_flash.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2s.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "zx_server.h"
#include "signal_from_zx.h"


static const char* TAG = "sfzx";
#define V_REF   1100

//i2s number
#define SFZX_I2S_NUM           (0)
//i2s sample rate
// need at least two sample points in one 256/6.5=39.4 us line, so less than 19us, for expmple 16.67us 
#define SFZX_I2S_SAMPLE_RATE   (60000) 
#define USEC_TO_SAMPLES(us)   (us*SFZX_I2S_SAMPLE_RATE/1000000) 
#define MILLISEC_TO_SAMPLES(ms)   (ms*SFZX_I2S_SAMPLE_RATE/1000) 
inline uint32_t samples_to_usec(uint32_t smpl)   { return (smpl*1000/(SFZX_I2S_SAMPLE_RATE/1000)); }



//i2s data bits
// ADC has 12 bit, so need 16
#define SFZX_I2S_SAMPLE_BITS   (16)

//I2S read buffer length in bytes
// we want to buffer several millisecs to be able to reschedule freely; on the
// other side, we do not want too many delays in order to be reactive, so say 10ms
#define SFZX_I2S_READ_LEN_SAMPLES      (SFZX_I2S_SAMPLE_RATE/100)
#define SFZX_I2S_READ_LEN_BYTES      (SFZX_I2S_READ_LEN_SAMPLES * SFZX_I2S_SAMPLE_BITS/8)


//I2S data format
// single channel is enough, the lest one seems to be connected to ADC
#define SFZX_I2S_FORMAT        (I2S_CHANNEL_FMT_ONLY_LEFT) 

//I2S channel number
#define SFZX_I2S_CHANNEL_NUM   ((SFZX_I2S_FORMAT < I2S_CHANNEL_FMT_ONLY_RIGHT) ? (2) : (1))
//I2S built-in ADC unit
#define I2S_ADC_UNIT              ADC_UNIT_1
//I2S built-in ADC channel
#define I2S_ADC_CHANNEL           ADC1_CHANNEL_0


//static  esp_adc_cal_characteristics_t sampleadc_characteristics;
static void sfzx_task(void*arg);


static QueueHandle_t i2squeue=NULL;
static uint8_t* i2s_read_buff=NULL;

typedef struct statistic_info
{
    uint32_t packets_received;
    uint16_t max_v;
    uint16_t min_v;
    uint16_t thresh_v;
    uint16_t last_v;
} statistic_info_type;

static statistic_info_type stat;    // some signal statistics





typedef struct level_status
{
	uint8_t current;
	uint32_t duration;
	uint32_t dur_since_last_sync;
	uint32_t dur_since_last_0_lvl;
} level_status_type;

static level_status_type level;    // some signal statistics



/**
 * @brief I2S ADC/DAC mode init.
 */
void sfzx_init()
{
	int i2s_num = SFZX_I2S_NUM;
	i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN,
        .sample_rate =  SFZX_I2S_SAMPLE_RATE,
        .bits_per_sample = SFZX_I2S_SAMPLE_BITS,
	    .communication_format =  I2S_COMM_FORMAT_I2S_MSB,
	    .channel_format = SFZX_I2S_FORMAT,
	    .intr_alloc_flags = 0,
	    .dma_buf_count = 4,
	    .dma_buf_len = SFZX_I2S_READ_LEN_SAMPLES,
	    .use_apll = 0,//1, True cause problems at high rates (?)
	};
	 //install and start i2s driver
    //xQueueCreate(5, sizeof(i2s_event_t));
	adc1_config_channel_atten(I2S_ADC_CHANNEL,ADC_ATTEN_DB_11);
	adc1_config_width(ADC_WIDTH_BIT_12);
	i2s_driver_install(i2s_num, &i2s_config, 5, &i2squeue );
	//init ADC pad
	i2s_set_adc_mode(I2S_ADC_UNIT, I2S_ADC_CHANNEL);

	//adc_power_always_on();
	i2s_adc_enable(SFZX_I2S_NUM);

	//adc_i2s_mode_init(I2S_ADC_UNIT, I2S_ADC_CHANNEL);
    // get scaling for conversion
    //esp_adc_cal_characterize(I2S_ADC_UNIT, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, V_REF, &sampleadc_characteristics);
    i2s_read_buff=(uint8_t*) calloc(SFZX_I2S_READ_LEN_BYTES, sizeof(uint8_t));
    memset(&stat,0,sizeof(statistic_info_type));
    xTaskCreate(sfzx_task, "sfzx_task", 1024 * 3, NULL, 8, NULL);


}


static inline uint16_t get_sample(void* samplebuf, uint32_t ix)
{
#if (ESP_IDF_VERSION_MAJOR>=4)
    return ((uint16_t*)samplebuf)[ix^1];
#else
    return 4095-((uint16_t*)samplebuf)[ix^1]; // early versions returned inverted data of ADC via I2S
#endif
}


static inline uint16_t calc_thresh_level(uint16_t minlevel, uint16_t maxlevel)
{
    return (minlevel+maxlevel+maxlevel)/3; // newrer to uppel level to get sync and black all to low side
}


static void create_initial_stat(void* samplebuf)
{
    uint32_t ix;
    uint16_t v=stat.max_v=stat.min_v=get_sample(samplebuf,0);

    for(ix=0;ix<SFZX_I2S_READ_LEN_SAMPLES;ix++)
    {
        v=get_sample(samplebuf,ix);
        if (v>stat.max_v) stat.max_v=v; 
        if (v<stat.min_v) stat.min_v=v; 
    }
    stat.thresh_v=(stat.max_v+stat.min_v)/2;
    stat.last_v=stat.thresh_v; // somewhere
}

static void do_some_stat(void* samplebuf)
{
    uint32_t ix;
    uint16_t v;

    // allow some recovery from spikes, but not too fast
    if ((stat.packets_received&0x0003)==2){ 
        stat.max_v--;
    }
    stat.min_v++; // adapt lower limit faster as there will always be hsyncs around

    // pick a subset of points to do some statistics quickly
    for(ix=SFZX_I2S_READ_LEN_SAMPLES/8; ix<SFZX_I2S_READ_LEN_SAMPLES/4;ix++)
    {
        v=get_sample(samplebuf,ix);
        if (v>stat.max_v) stat.max_v=v; 
        if (v<stat.min_v) stat.min_v=v; 
    }
    stat.thresh_v=(stat.max_v+stat.min_v)/2;
}

static zxserv_evt_type_t phase=ZXSG_INIT;

static const char* phasenames[]={"INIT","SLOW-50Hz","SLOW-60Hz","SAVE","SILENCE","HIGH","NOISE"};

static void set_det_phase(zxserv_evt_type_t newphase)
{
	if(newphase!=phase){
		phase=newphase;
		zxsrv_send_msg_to_srv( newphase, 0,0);
		ESP_LOGI(TAG,"Detected %s \n", phasenames[newphase-ZXSG_INIT]  );
	}
}


typedef enum {
    ZXFS_INIT     = 0,        /*!< initial status */
    ZXFS_HDR_RECEIVED   = 1,        /*!< start retriving file */
    ZXFS_RETRIEVE_DATA   = 2,                 /*!< during file transfer, check if ZX loads or ignores */
} zxfs_state_t;


typedef struct zxfile_rec_status_info
{
    zxfs_state_t state;
    uint8_t pulscount;
    uint8_t bitcount;
    uint8_t data;
    uint16_t e_line;
    uint16_t bytecount;
    uint16_t namelength;
} zxfile_rec_status_t;

static zxfile_rec_status_t zxfile;    // some signal statistics


static void zxfile_bit(uint8_t bitval)
{
    if(bitval) zxfile.data |= (0x80>>zxfile.bitcount);
    if(++zxfile.bitcount>=8) {
        // have a byte
        if(zxfile.bytecount%100<=1) ESP_LOGI(TAG,"ZXFile byte %d data %x\n",zxfile.bytecount,zxfile.data );
        if(zxfile.bytecount == zxfile.namelength+16404-16393) zxfile.e_line=zxfile.data;
        if(zxfile.bytecount == zxfile.namelength+16405-16393) {
            zxfile.e_line+=zxfile.data<<8;
            ESP_LOGI(TAG,"File E_LINE %d - len %d+%d\n",zxfile.e_line,zxfile.e_line-16393,zxfile.namelength);
        }
		zxsrv_send_msg_to_srv( ZXSG_FILE_DATA, zxfile.bytecount, zxfile.data);

        zxfile.bitcount=0;
        zxfile.bytecount++;
        // zx memory image is preceided by a name that end with the first inverse char (MSB set)
        if (zxfile.namelength==0 && (zxfile.data&0x80) ) zxfile.namelength=zxfile.bytecount;
        zxfile.data=0;
        set_det_phase(ZXSG_SAVE);
    }
}

static void zxfile_check_bit_end()
{
    //if(zxfile.bytecount%50==2) printf(" ZXFile bit %d pulses (%d us) \n",zxfile.pulscount,samples_to_usec(level.duration) );
    // test have shown that the 4 and 9 pulses are retrieved quite precisely, nevertheless add some tolerance
    if(zxfile.pulscount>=3 && zxfile.pulscount<=5){
        zxfile_bit(0);
    }
    else if(zxfile.pulscount>=7 && zxfile.pulscount<=11){
        zxfile_bit(1);
    }
    else{
        ESP_LOGI(TAG,"File read retrieved %d pulses, cancel\n",zxfile.pulscount);
        zxfile.state=ZXFS_INIT;
    }
    zxfile.pulscount=0;
}


static void analyze_1_to_0()
{
	// end of high phase
	if (zxfile.state>=ZXFS_HDR_RECEIVED){
		if(level.duration>=USEC_TO_SAMPLES(90) && level.duration<=USEC_TO_SAMPLES(250)){ // should be 150u
			++zxfile.pulscount;
		}
	}
	else
	{
		 if ( level.dur_since_last_sync>MILLISEC_TO_SAMPLES(100) )
		 {
			 // No file but also no display nor silence
		//	 if(level.duration>USEC_TO_SAMPLES(100)&&level.duration<USEC_TO_SAMPLES(1000)) set_det_phase(ZXSG_NOISE);
		 }
	}
}


static void analyze_0_to_1()
{
	if(level.duration>2) level.dur_since_last_0_lvl=0;

	if(level.duration>USEC_TO_SAMPLES(300) && level.duration<USEC_TO_SAMPLES(600))
	{
		// could be sync
		if(level.dur_since_last_sync>MILLISEC_TO_SAMPLES(15) && level.dur_since_last_sync<MILLISEC_TO_SAMPLES(22))
		{
			set_det_phase(ZXSG_SLOWM_50HZ);
		}
		else if(level.dur_since_last_sync<MILLISEC_TO_SAMPLES(10)){
			set_det_phase(ZXSG_NOISE);
		}

		if(stat.packets_received%5000==10){
			ESP_LOGI(TAG,"Frame detected %d usec (%d) ", samples_to_usec(level.dur_since_last_sync),level.duration  );
			ESP_LOGI(TAG,"Min Max Thresh %d %d %d \n", stat.min_v, stat.max_v, stat.thresh_v  );

		}
		level.dur_since_last_sync=0;
	}

	if (zxfile.state>=ZXFS_HDR_RECEIVED){
		if(level.duration<USEC_TO_SAMPLES(250)){
				// okay, gap should be 150u for pules
		} else if (level.duration>USEC_TO_SAMPLES(1200) && level.duration<USEC_TO_SAMPLES(1800)){ // 1.3ms+0.15
			zxfile_check_bit_end();
		}
		else
		{
			ESP_LOGI(TAG,"File gap retrieved of %d usec, cancel with %d bytes\n",samples_to_usec(level.duration),zxfile.bytecount);
			zxfile.state=ZXFS_INIT;
		}
	} else {
		 if ( level.dur_since_last_sync>MILLISEC_TO_SAMPLES(100) )
		 {
			 // No file but also no display nor silence
			 if(level.duration>USEC_TO_SAMPLES(300)&&level.duration<USEC_TO_SAMPLES(2000)) set_det_phase(ZXSG_NOISE);
		 }

	}


	if(zxfile.state<ZXFS_HDR_RECEIVED && level.duration>MILLISEC_TO_SAMPLES(100))
	{
		// end of long break, could be header of file
		memset(&zxfile,0,sizeof(zxfile_rec_status_t));
		zxfile.state=ZXFS_HDR_RECEIVED;
	}
}




static void analyze_static_level()
{
	if (level.current==0) set_det_phase(ZXSG_SILENCE);

	if (level.current==1) set_det_phase(ZXSG_HIGH);
}



static void analyze_for_noise()
{
	if(level.dur_since_last_0_lvl>MILLISEC_TO_SAMPLES(20)) {
		if(level.current==1 || level.duration<4)
			set_det_phase(ZXSG_HIGH);
	}
}

//static char debugbuf[250];

#define INITIAL_IGNORED_PACKETS 20

static void analyze_data(uint8_t* buf, size_t size)
{
    uint32_t ix;
    uint16_t v;

	++stat.packets_received;

	if(stat.packets_received<INITIAL_IGNORED_PACKETS)
		return;
	else if(stat.packets_received==INITIAL_IGNORED_PACKETS)
		create_initial_stat(buf);
	else
		do_some_stat(buf);

	if(stat.packets_received%8000==32 ){
		ESP_LOGV(TAG,"stat.packets_received %d \n",stat.packets_received);
		ESP_LOGI(TAG,"Min Max Thresh %d %d %d \n", stat.min_v, stat.max_v, stat.thresh_v  );
		ESP_LOGI(TAG,"Current %d %d %d,  \n", level.current, level.duration,level.dur_since_last_0_lvl  );
//		vTaskList( debugbuf ); // TODO - also disable this in menuconfig if we want to save rsources and remove this
//		ESP_LOGI(TAG,"\n%s\n", debugbuf);
	}
	for(ix=0;ix<SFZX_I2S_READ_LEN_SAMPLES;ix++) {
		v=get_sample(buf,ix);
		++level.duration; // might be overritten to zero if we the level changes
		++level.dur_since_last_sync;
		++level.dur_since_last_0_lvl;
		if (level.current==1){ // was high level
			// ignore single short spikes, could by H-syncs
			if( v < stat.thresh_v &&  stat.last_v < stat.thresh_v)
			{
				if(level.duration>2)
					analyze_1_to_0();
				level.current=0;
				level.duration=0;
			}

			if (level.dur_since_last_0_lvl%MILLISEC_TO_SAMPLES(500)==MILLISEC_TO_SAMPLES(50)){
				analyze_for_noise();
			}

		}
		else // was low level
		{
			if( v > stat.thresh_v &&  (zxfile.state==ZXFS_INIT || stat.last_v > stat.thresh_v)  ) // we do not expect spikes here, de-glich when listening to file
			{
				// end of low phase
				if(level.duration>2) analyze_0_to_1();
				level.current=1;
				level.duration=0;
			}
		}

		if (level.duration%MILLISEC_TO_SAMPLES(500)==MILLISEC_TO_SAMPLES(100)){
			analyze_static_level();
		}




		stat.last_v=v;
	}

}



static void sfzx_task(void*arg)
{
    i2s_event_t evt;
	size_t bytes_read;
	uint8_t ignore_first_packets=20;
    ESP_LOGI(TAG,"sfzx_task START \n");
	//vTaskDelay(100 / portTICK_PERIOD_MS); // allow some startup and settling time (might help, not proven)
	//i2s_adc_enable(SFZX_I2S_NUM);
    while(true){
		if(pdTRUE ==  xQueueReceive( i2squeue, &evt, 10 / portTICK_RATE_MS ) ) {
			if(evt.type==I2S_EVENT_RX_DONE){
                bytes_read=0;
                i2s_read(SFZX_I2S_NUM, (void*) i2s_read_buff, SFZX_I2S_READ_LEN_BYTES, &bytes_read, 0); // should always succeed with fll length
                if (bytes_read!=SFZX_I2S_READ_LEN_BYTES){
                    ESP_LOGW(TAG, "len mismatch, %d %d",bytes_read,SFZX_I2S_READ_LEN_BYTES);
                }
				if (ignore_first_packets)
					ignore_first_packets--;
				else
                	analyze_data(i2s_read_buff,SFZX_I2S_READ_LEN_BYTES);
			}
			else ESP_LOGW(TAG,"Unexpected evt %d",evt.type);
		}
    }
}


