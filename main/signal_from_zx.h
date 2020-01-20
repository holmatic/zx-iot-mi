// holmatic


#ifndef _SIGNAL_FROM_ZX_H_
#define _SIGNAL_FROM_ZX_H_
#include "esp_err.h"
#include <esp_types.h>
#include "esp_attr.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// call once at startup
void sfzx_init();

typedef enum {
    SFZX_MODE_IDLE    = 1,                      /*!< ignore retrieved data */
    SFZX_MODE_LISTEN   = 2,   /*!< check if ZX sends a file or wants to retrieve */
    SFZX_MODE_CHECK_ECHO   = 3,                 /*!< during file transfer, check if ZX loads or ignores */
} sfzx_mode_t;

typedef enum {
    SFZX_EVTTYPE_SILENCE     = 101,        /*!< silence longer than for saving */
    SFZX_EVTTYPE_FILESTART   = 102,        /*!< start retriving file */
    SFZX_EVTTYPE_FILE_DATA   = 103,                 /*!< during file transfer, check if ZX loads or ignores */
} sfzx_evt_type_t;

typedef struct {
    uint8_t  etype;   /*!< sfzx_evt_type_t */
    uint8_t  data;    /*!< for SFZX_EVTTYPE_FILE_DATA */
} sfzx_event_t;


typedef enum {
    ZXSG_INIT       = 0,        /*!< initial status */
    ZXSG_SLOWM_50HZ  ,        /*!< start retriving file */
    ZXSG_SLOWM_60HZ,                 /*!< during file transfer, check if ZX loads or ignores */
	ZXSG_SAVE,
	ZXSG_SILENCE,
	ZXSG_HIGH ,
	ZXSG_NOISE
} zx_det_phase_t;


// call periodically
void sfzx_handle(sfzx_mode_t current_mode, QueueHandle_t fill_events_here);

#ifdef __cplusplus
}
#endif

#endif /* _SIGNAL_FROM_ZX_H_ */
